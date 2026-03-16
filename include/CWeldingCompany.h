#pragma once

#include <vector>
#include <unordered_map>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include "../common.h"

/**
 * @brief Concurrent welding order processing company.
 *
 * Accepts price lists from multiple producers, receives orders from customers,
 * and solves the optimal cutting/welding problem using dynamic programming.
 * All order fulfillment is parallelised across a configurable worker-thread pool.
 */
class CWeldingCompany{
public:
    // -----------------------------------------------------------------------
    //  Sequential solver (also used internally by worker threads)
    // -----------------------------------------------------------------------

    /**
     * @brief Solve a single order against one price list.
     *
     * Fills @p order.m_Cost with the minimum achievable cost.
     * If no valid decomposition exists, m_Cost is set to
     * std::numeric_limits<double>::max().
     *
     * @param priceList  Available panel sizes and their unit costs.
     * @param order      Order to price (m_Cost is written here).
     */
    static void seqSolve(APriceList priceList, COrder& order);

    // -----------------------------------------------------------------------
    //  Registration
    // -----------------------------------------------------------------------

    /**
     * Register a material producer.  Must be called before start().
     */
    void addProducer(AProducer prod);

    /**
     * Register a customer whose demands will be served. Must be called before start().
     */
    void addCustomer(ACustomer cust);

    // -----------------------------------------------------------------------
    //  Price-list ingestion  (called by producers, possibly from any thread)
    // -----------------------------------------------------------------------

    /**
     * @brief Receive a price list from a producer.
     *
     * Thread-safe.  Merges the new list into the per-material catalogue,
     * keeping the cheapest price for each panel size.
     */
    void addPriceList(AProducer prod, const APriceList& priceList);

    // -----------------------------------------------------------------------
    //  Life-cycle
    // -----------------------------------------------------------------------

    /**
     * @brief Launch @p thrCount worker threads and one serving thread per customer.
     * @param thrCount  Number of order-processing worker threads.
     */
    void start(unsigned thrCount);

    /**
     * @brief Wait for all work to finish and join every thread.
     *
     * Blocks until every customer has no more pending orders and all
     * worker threads have exited cleanly.
     */
    void stop();

private:
    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    static void setMinCost(
        std::unordered_map<unsigned, std::unordered_map<unsigned, double>>& dp,
        unsigned w, unsigned h, double cost);

    static double mySolve(
        std::unordered_map<unsigned, std::unordered_map<unsigned, double>>& dp,
        unsigned w, unsigned h, double weldingStrength);

    // -----------------------------------------------------------------------
    //  Internal types
    // -----------------------------------------------------------------------

    /**
     * @brief Descriptor for a single order slot inside an order list.
     *
     * Worker threads pick these from the queue and call seqSolve() on the
     * referenced order.  Once every slot in the list is done the customer
     * is notified.
     */
    struct OrderSlot{
        ACustomer customer;
        AOrderList orderList;
        unsigned orderIndex{};
        unsigned totalOrders{};
        std::atomic<unsigned>* completedOrders{};
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
    std::mutex m_prodMtx;

    std::vector<ACustomer> m_customers;

    /* Thread pools ---------------------------------------------------------*/
    std::vector<std::thread> m_workingThreads;
    std::vector<std::thread> m_servingThreads;

    std::atomic<bool> m_stopped{false};

    /* Price-list catalogue -------------------------------------------------*/
    std::unordered_map<unsigned, APriceList> m_materialPriceLists;
    std::mutex m_priceListsMtx;
    std::condition_variable m_cv_priceLists;

    /* How many producers have responded for each material ------------------*/
    std::unordered_map<unsigned, unsigned> m_materialProducerCount;
    std::mutex m_matProdCountMtx;
    std::condition_variable m_cv_matProdCount;

    /* Order queue ----------------------------------------------------------*/
    static constexpr unsigned kMaxQueueSize = 150;

    std::queue<OrderSlot> m_orderQueue;
    std::mutex m_orderQueueMtx;
    std::condition_variable m_cv_orderQueue;
    std::condition_variable m_cv_queueRoom;

    /* Active-order tracking ------------------------------------------------*/
    std::atomic<unsigned> m_activeOrders{0};
    std::mutex m_activeOrdersMtx;
    std::condition_variable m_cv_activeOrders;
};
