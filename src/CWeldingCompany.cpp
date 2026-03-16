#include "../include/CWeldingCompany.h"

#include <limits>
#include <algorithm>

// ============================================================================
//  Static helpers
// ============================================================================

void CWeldingCompany::setMinCost(
    std::unordered_map<unsigned, std::unordered_map<unsigned, double>>& dp,
    const unsigned w, const unsigned h, const double cost){
    auto& row = dp[w];
    if (const auto it = row.find(h); it == row.end() || cost < it->second)
        row[h] = cost;
}

double CWeldingCompany::mySolve(
    std::unordered_map<unsigned, std::unordered_map<unsigned, double>>& dp,
    const unsigned w, const unsigned h, const double weldingStrength){
    // Base case: exact panel available
    if (auto wi = dp.find(w); wi != dp.end())
        if (auto hi = wi->second.find(h); hi != wi->second.end())
            return hi->second;

    double minCost = std::numeric_limits<double>::max();

    // Horizontal splits (constant height, varying width)
    for (unsigned xSplit = 1; xSplit <= w / 2; ++xSplit){
        const double lc = mySolve(dp, xSplit, h, weldingStrength);
        const double rc = mySolve(dp, w - xSplit, h, weldingStrength);
        if (lc < std::numeric_limits<double>::max() &&
            rc < std::numeric_limits<double>::max()){
            minCost = std::min(minCost, lc + rc + h * weldingStrength);
        }
    }

    // Vertical splits (constant width, varying height)
    for (unsigned ySplit = 1; ySplit <= h / 2; ++ySplit){
        const double tc = mySolve(dp, w, ySplit, weldingStrength);
        const double bc = mySolve(dp, w, h - ySplit, weldingStrength);
        if (tc < std::numeric_limits<double>::max() &&
            bc < std::numeric_limits<double>::max()){
            minCost = std::min(minCost, tc + bc + w * weldingStrength);
        }
    }

    // Cache result (even if no solution found — avoids re-exploration)
    setMinCost(dp, w, h, minCost);
    return minCost;
}

// ============================================================================
//  Sequential solver (public)
// ============================================================================

void CWeldingCompany::seqSolve(APriceList priceList, COrder& order){
    // dp[w][h] = cheapest known price for a panel of size w×h
    std::unordered_map<unsigned, std::unordered_map<unsigned, double>> dp;

    for (const auto& prod : priceList->m_List){
        setMinCost(dp, prod.m_W, prod.m_H, prod.m_Cost);
        if (prod.m_W != prod.m_H) // non-square: both orientations
            setMinCost(dp, prod.m_H, prod.m_W, prod.m_Cost);
    }

    order.m_Cost = mySolve(dp, order.m_W, order.m_H, order.m_WeldingStrength);
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
    const unsigned mid = priceList->m_MaterialID;

    {
        std::lock_guard lg(m_priceListsMtx);

        auto it = m_materialPriceLists.find(mid);
        if (it == m_materialPriceLists.end()){
            m_materialPriceLists[mid] = priceList;
        } else{
            // Merge: keep the cheapest price per panel shape
            for (const auto& newProd : priceList->m_List){
                bool found = false;
                for (auto& existing : it->second->m_List){
                    bool sameShape =
                        (newProd.m_W == existing.m_W && newProd.m_H == existing.m_H) ||
                        (newProd.m_W == existing.m_H && newProd.m_H == existing.m_W);
                    if (sameShape){
                        if (newProd.m_Cost < existing.m_Cost)
                            existing.m_Cost = newProd.m_Cost;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    it->second->add(newProd);
            }
        }
    }

    {
        std::lock_guard lg(m_matProdCountMtx);
        ++m_materialProducerCount[mid];
        m_cv_matProdCount.notify_all();
    }
    m_cv_priceLists.notify_all();
}

// ============================================================================
//  Life-cycle
// ============================================================================

void CWeldingCompany::start(unsigned thrCount){
    for (unsigned i = 0; i < thrCount; ++i)
        m_workingThreads.emplace_back(&CWeldingCompany::workingThrFun, this);

    for (const auto& cust : m_customers)
        m_servingThreads.emplace_back(&CWeldingCompany::servingThrFun, this, cust);
}

void CWeldingCompany::stop(){
    // Wait for all serving threads (customers) to finish
    for (auto& t : m_servingThreads)
        t.join();

    // Signal workers: no more work will arrive
    {
        std::lock_guard lg(m_orderQueueMtx);
        m_stopped = true;
        m_cv_orderQueue.notify_all();
    }

    // Wait until every in-flight order has been processed
    {
        std::unique_lock ul(m_activeOrdersMtx);
        m_cv_activeOrders.wait(ul, [this]{ return m_activeOrders == 0; });
    }

    for (auto& t : m_workingThreads)
        t.join();
}

// ============================================================================
//  Internal thread functions
// ============================================================================

void CWeldingCompany::servingThrFun(const ACustomer& cust){
    while (true){
        const AOrderList orderList = cust->waitForDemand();
        if (!orderList) break;

        const auto total = static_cast<unsigned>(orderList->m_List.size());
        auto* counter = new std::atomic<unsigned>(0);

        for (unsigned i = 0; i < total; ++i){
            std::unique_lock ul(m_orderQueueMtx);
            m_cv_queueRoom.wait(ul,
                                [this]{ return m_orderQueue.size() < kMaxQueueSize; });

            m_orderQueue.push(OrderSlot{cust, orderList, i, total, counter});

            {
                std::lock_guard lg(m_activeOrdersMtx);
                ++m_activeOrders;
            }

            ul.unlock();
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
                                 [this]{ return !m_orderQueue.empty() || m_stopped.load(); });

            if (m_stopped && m_orderQueue.empty()) break;
            if (m_orderQueue.empty()) continue;

            slot = m_orderQueue.front();
            m_orderQueue.pop();
            m_cv_queueRoom.notify_one();
        }

        fulfillOrder(slot);

        {
            std::lock_guard lg(m_activeOrdersMtx);
            --m_activeOrders;
        }
        m_cv_activeOrders.notify_one();
    }
}

// ============================================================================
//  Price-list synchronisation
// ============================================================================

void CWeldingCompany::waitForMaterialPriceList(unsigned materialID){
    std::unique_lock ul(m_matProdCountMtx);

    if (m_materialProducerCount.find(materialID) == m_materialProducerCount.end()){
        m_materialProducerCount[materialID] = 0;
        ul.unlock();

        {
            std::lock_guard lg(m_prodMtx);
            for (const auto& p : m_producers)
                p->sendPriceList(materialID);
        }

        ul.lock();
    }

    const unsigned expected = static_cast<unsigned>(m_producers.size());
    m_cv_matProdCount.wait(ul,
                           [&]{ return m_materialProducerCount[materialID] >= expected; });
}

// ============================================================================
//  Order fulfillment
// ============================================================================

void CWeldingCompany::fulfillOrder(const OrderSlot& slot){
    waitForMaterialPriceList(slot.orderList->m_MaterialID);

    APriceList pl;
    {
        std::lock_guard lg(m_priceListsMtx);
        pl = m_materialPriceLists.at(slot.orderList->m_MaterialID);
    }

    seqSolve(pl, slot.orderList->m_List[slot.orderIndex]);

    if (++(*slot.completedOrders) == slot.totalOrders){
        slot.customer->completed(slot.orderList);
        delete slot.completedOrders;
    }
}
