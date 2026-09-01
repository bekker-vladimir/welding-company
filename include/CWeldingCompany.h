#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <atomic>
#include <memory>
#include "../common.h"

/**
 * @brief Concurrent welding order processing company.
 *
 * Accepts price lists from multiple producers, receives orders from customers,
 * and solves the optimal cutting/welding problem using dynamic programming.
 * Order fulfillment is parallelised across a configurable worker-thread pool
 * fed by a bounded queue (std::counting_semaphore provides the backpressure).
 *
 * Single-use: after stop() the instance must not be started again.
 */
class CWeldingCompany{
public:
    CWeldingCompany() = default;

    /** Joins any still-running threads so a missed stop() cannot make
     *  ~std::thread call std::terminate(). */
    ~CWeldingCompany();

    CWeldingCompany(const CWeldingCompany&) = delete;
    CWeldingCompany& operator=(const CWeldingCompany&) = delete;

    // -----------------------------------------------------------------------
    //  Sequential solver (also used internally by worker threads)
    // -----------------------------------------------------------------------

    /**
     * @brief Solve a single order against one price list.
     *
     * Fills @p order.m_Cost with the minimum achievable cost, considering both
     * buying the panel outright and welding it from cheaper pieces.
     * If no valid decomposition exists, m_Cost is set to
     * std::numeric_limits<double>::max().
     */
    static void seqSolve(APriceList priceList, COrder& order);

    // -----------------------------------------------------------------------
    //  Registration  (must be called before start())
    // -----------------------------------------------------------------------

    void addProducer(AProducer prod);
    void addCustomer(ACustomer cust);

    // -----------------------------------------------------------------------
    //  Price-list ingestion  (called by producers, possibly from any thread)
    // -----------------------------------------------------------------------

    /**
     * @brief Receive a price list from a producer.
     *
     * Thread-safe.  Copies the incoming data into a catalogue owned by this
     * class, keeping the cheapest price for each panel shape (rotations count
     * as the same shape).
     */
    void addPriceList(AProducer prod, const APriceList& priceList);

    // -----------------------------------------------------------------------
    //  Life-cycle
    // -----------------------------------------------------------------------

    /** Launch @p thrCount worker threads and one serving thread per customer. */
    void start(unsigned thrCount);

    /** Drain all in-flight orders and join every thread.  Idempotent. */
    void stop();

private:
    // -----------------------------------------------------------------------
    //  DP solver
    // -----------------------------------------------------------------------

    /**
     * @brief Memoisation state for one (price list, welding strength) pair.
     *
     * Shapes are normalised to w <= h, so 3x5 and 5x3 share a cache entry.
     * `direct` and `solved` must stay separate: a size present in the
     * catalogue may still be cheaper to weld from pieces, so a catalogue hit
     * is an opening bid, never a final answer.
     */
    struct DPCache{
        std::unordered_map<std::uint64_t, double> direct; // catalogue prices
        std::unordered_map<std::uint64_t, double> solved; // fully solved sizes
        double weld{};
    };

    static std::uint64_t shapeKey(unsigned w, unsigned h);
    static double mySolve(DPCache& cache, unsigned w, unsigned h);

    // -----------------------------------------------------------------------
    //  Internal types
    // -----------------------------------------------------------------------

    /**
     * @brief One order plus the bookkeeping needed to notify the customer.
     *
     * The worker that drives `completedOrders` up to `totalOrders` is the last
     * one out and calls completed().  The counter is shared_ptr-owned so an
     * exception in the solver cannot leak it.
     */
    struct OrderSlot{
        ACustomer customer;
        AOrderList orderList;
        unsigned orderIndex{};
        unsigned totalOrders{};
        std::shared_ptr<std::atomic<unsigned>> completedOrders;
    };

    void servingThrFun(const ACustomer& cust);
    void workingThrFun();
    void waitForMaterialPriceList(unsigned materialID);
    void fulfillOrder(const OrderSlot& slot);

    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------

    /* Producers / customers ------------------------------------------------*/
    std::vector<AProducer> m_producers;
    std::vector<ACustomer> m_customers;

    /* Thread pools ---------------------------------------------------------*/
    std::vector<std::thread> m_workingThreads;
    std::vector<std::thread> m_servingThreads;

    /* Price-list catalogue, owned by us ------------------------------------*/
    std::unordered_map<unsigned, APriceList> m_materialPriceLists;
    // materialID -> shape key -> index into that list's m_List  (O(1) merge)
    std::unordered_map<unsigned, std::unordered_map<std::uint64_t, std::size_t>> m_shapeIndex;
    std::mutex m_priceListsMtx;

    /* Which materials we already asked about, and how many producers replied.
       These are deliberately separate: an unsolicited price list bumps the
       counter but must not make us think the request was already sent. -----*/
    std::unordered_set<unsigned> m_requestedMaterials;
    std::unordered_map<unsigned, unsigned> m_materialProducerCount;
    std::mutex m_matProdCountMtx;
    std::condition_variable m_cv_matProdCount;

    /* Order queue ----------------------------------------------------------*/
    static constexpr std::ptrdiff_t kMaxQueueSize = 150;

    std::queue<OrderSlot> m_orderQueue;
    std::mutex m_orderQueueMtx; // guards queue, m_stopped, m_activeOrders
    std::condition_variable m_cv_orderQueue;
    std::condition_variable m_cv_activeOrders;
    bool m_stopped{false};
    unsigned m_activeOrders{0};

    /** Free slots in the bounded queue.  Serving threads acquire before
     *  pushing, workers release after popping - this is the backpressure. */
    std::counting_semaphore<kMaxQueueSize> m_queueRoom{kMaxQueueSize};
};
