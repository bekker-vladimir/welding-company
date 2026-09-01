#include "../include/CWeldingCompany.h"

#include <limits>
#include <algorithm>
#include <utility>

namespace {
constexpr double kImpossible = std::numeric_limits<double>::max();
}

// ============================================================================
//  DP solver
// ============================================================================

std::uint64_t CWeldingCompany::shapeKey(const unsigned w, const unsigned h){
    // Normalised so that a WxH panel and an HxW panel hash to one entry.
    const unsigned lo = w < h ? w : h;
    const unsigned hi = w < h ? h : w;
    return (static_cast<std::uint64_t>(lo) << 32) | hi;
}

double CWeldingCompany::mySolve(DPCache& cache, unsigned w, unsigned h){
    if (w > h) std::swap(w, h);                 // rotations are equivalent
    const std::uint64_t key = shapeKey(w, h);

    if (const auto it = cache.solved.find(key); it != cache.solved.end())
        return it->second;

    // A catalogue entry is the opening bid, not the answer: welding the same
    // size out of cheaper pieces may still beat buying it outright.
    double best = kImpossible;
    if (const auto it = cache.direct.find(key); it != cache.direct.end())
        best = it->second;

    // Horizontal splits: cut across the width, seam runs the full height.
    for (unsigned x = 1; x <= w / 2; ++x){
        const double lc = mySolve(cache, x, h);
        const double rc = mySolve(cache, w - x, h);
        if (lc < kImpossible && rc < kImpossible)
            best = std::min(best, lc + rc + h * cache.weld);
    }

    // Vertical splits: cut across the height, seam runs the full width.
    for (unsigned y = 1; y <= h / 2; ++y){
        const double tc = mySolve(cache, w, y);
        const double bc = mySolve(cache, w, h - y);
        if (tc < kImpossible && bc < kImpossible)
            best = std::min(best, tc + bc + w * cache.weld);
    }

    // Cached even when impossible, so the subtree is never re-explored.
    cache.solved.emplace(key, best);
    return best;
}

void CWeldingCompany::seqSolve(APriceList priceList, COrder& order){
    DPCache cache;
    cache.weld = order.m_WeldingStrength;

    if (priceList){
        for (const auto& prod : priceList->m_List){
            const std::uint64_t key = shapeKey(prod.m_W, prod.m_H);
            const auto it = cache.direct.find(key);
            if (it == cache.direct.end() || prod.m_Cost < it->second)
                cache.direct[key] = prod.m_Cost;
        }
    }

    order.m_Cost = mySolve(cache, order.m_W, order.m_H);
}

// ============================================================================
//  Registration
// ============================================================================

void CWeldingCompany::addProducer(AProducer prod){
    m_producers.push_back(std::move(prod));
}

void CWeldingCompany::addCustomer(ACustomer cust){
    m_customers.push_back(std::move(cust));
}

// ============================================================================
//  Price-list ingestion
// ============================================================================

void CWeldingCompany::addPriceList(AProducer /*prod*/, const APriceList& priceList){
    if (!priceList) return;
    const unsigned mid = priceList->m_MaterialID;

    {
        std::lock_guard lg(m_priceListsMtx);

        // We copy into a catalogue we own: the producer keeps its own object,
        // and nothing we hand to the solver can be mutated behind its back.
        auto& own = m_materialPriceLists[mid];
        if (!own) own = std::make_shared<CPriceList>(mid);
        auto& index = m_shapeIndex[mid];

        for (const auto& incoming : priceList->m_List){
            const std::uint64_t key = shapeKey(incoming.m_W, incoming.m_H);
            const auto it = index.find(key);
            if (it == index.end()){
                index.emplace(key, own->m_List.size());
                own->add(incoming);
            } else if (incoming.m_Cost < own->m_List[it->second].m_Cost){
                own->m_List[it->second].m_Cost = incoming.m_Cost;
            }
        }
    }

    {
        std::lock_guard lg(m_matProdCountMtx);
        ++m_materialProducerCount[mid];
    }
    m_cv_matProdCount.notify_all();
}

// ============================================================================
//  Life-cycle
// ============================================================================

CWeldingCompany::~CWeldingCompany(){
    stop();
}

void CWeldingCompany::start(unsigned thrCount){
    for (unsigned i = 0; i < thrCount; ++i)
        m_workingThreads.emplace_back(&CWeldingCompany::workingThrFun, this);

    for (const auto& cust : m_customers)
        m_servingThreads.emplace_back(&CWeldingCompany::servingThrFun, this, cust);
}

void CWeldingCompany::stop(){
    // 1. Serving threads first: once they are gone, no new work can arrive.
    for (auto& t : m_servingThreads)
        if (t.joinable()) t.join();
    m_servingThreads.clear();

    // 2. Tell the workers no more work is coming.
    {
        std::lock_guard lg(m_orderQueueMtx);
        m_stopped = true;
    }
    m_cv_orderQueue.notify_all();

    // 3. Wait until every order already taken off the queue is finished.
    {
        std::unique_lock ul(m_orderQueueMtx);
        m_cv_activeOrders.wait(ul, [this]{ return m_activeOrders == 0; });
    }

    for (auto& t : m_workingThreads)
        if (t.joinable()) t.join();
    m_workingThreads.clear();
}

// ============================================================================
//  Internal thread functions
// ============================================================================

void CWeldingCompany::servingThrFun(const ACustomer& cust){
    while (true){
        const AOrderList orderList = cust->waitForDemand();
        if (!orderList) break;

        const auto total = static_cast<unsigned>(orderList->m_List.size());

        // An empty batch has no slot to trigger the callback, so answer here
        // or the customer waits forever.
        if (total == 0){
            cust->completed(orderList);
            continue;
        }

        auto counter = std::make_shared<std::atomic<unsigned>>(0);

        for (unsigned i = 0; i < total; ++i){
            m_queueRoom.acquire();   // blocks while the queue is full

            {
                std::lock_guard lg(m_orderQueueMtx);
                m_orderQueue.push(OrderSlot{cust, orderList, i, total, counter});
                ++m_activeOrders;
            }
            m_cv_orderQueue.notify_one();
        }
    }
}

void CWeldingCompany::workingThrFun(){
    while (true){
        OrderSlot slot;
        {
            std::unique_lock ul(m_orderQueueMtx);
            m_cv_orderQueue.wait(ul,
                                 [this]{ return !m_orderQueue.empty() || m_stopped; });

            // Empty here implies m_stopped: drain first, exit second.
            if (m_orderQueue.empty()) break;

            slot = std::move(m_orderQueue.front());
            m_orderQueue.pop();
        }
        m_queueRoom.release();       // one slot freed

        fulfillOrder(slot);

        {
            std::lock_guard lg(m_orderQueueMtx);
            --m_activeOrders;
        }
        m_cv_activeOrders.notify_all();
    }
}

// ============================================================================
//  Price-list synchronisation
// ============================================================================

void CWeldingCompany::waitForMaterialPriceList(unsigned materialID){
    std::unique_lock ul(m_matProdCountMtx);

    // insert() tells us whether we are the first to ask.  This is tracked
    // separately from the reply counter on purpose: a producer may push a
    // price list nobody asked for, and that must not be mistaken for "the
    // request has already gone out".
    if (m_requestedMaterials.insert(materialID).second){
        // Unlock first - a synchronous producer calls addPriceList() from
        // inside sendPriceList(), which takes this very mutex.
        ul.unlock();
        for (const auto& p : m_producers)
            p->sendPriceList(materialID);
        ul.lock();
    }

    const auto expected = static_cast<unsigned>(m_producers.size());
    m_cv_matProdCount.wait(ul,
                           [&]{ return m_materialProducerCount[materialID] >= expected; });
}

// ============================================================================
//  Order fulfillment
// ============================================================================

void CWeldingCompany::fulfillOrder(const OrderSlot& slot){
    const unsigned mid = slot.orderList->m_MaterialID;
    waitForMaterialPriceList(mid);

    // Snapshot under the lock: a late price list must not resize the vector
    // while the solver is walking it.
    APriceList snapshot;
    {
        std::lock_guard lg(m_priceListsMtx);
        if (const auto it = m_materialPriceLists.find(mid); it != m_materialPriceLists.end())
            snapshot = std::make_shared<CPriceList>(*it->second);
    }

    COrder& order = slot.orderList->m_List[slot.orderIndex];
    if (snapshot)
        seqSolve(snapshot, order);
    else
        order.m_Cost = kImpossible;   // no producer supplies this material

    if (++(*slot.completedOrders) == slot.totalOrders)
        slot.customer->completed(slot.orderList);
}