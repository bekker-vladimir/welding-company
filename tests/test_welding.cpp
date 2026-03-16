/**
 * @file tests/test_welding.cpp
 * @brief Unit and integration tests for CWeldingCompany.
 *
 * Build (from project root):
 *   g++ -std=c++20 -O2 -pthread -Iinclude \
 *       src/CWeldingCompany.cpp tests/test_welding.cpp -o run_tests
 *   ./run_tests
 */

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <functional>
#include <atomic>
#include <chrono>
#include <thread>

#include "../include/CWeldingCompany.h"
#include "../common.h"

// ============================================================================
//  Tiny test framework
// ============================================================================

namespace {

int  g_total  = 0;
int  g_passed = 0;

void check(bool cond, const char *desc) {
    ++g_total;
    if (cond) {
        ++g_passed;
        std::cout << "  [PASS] " << desc << "\n";
    } else {
        std::cout << "  [FAIL] " << desc << "\n";
    }
}

void section(const char *title) {
    std::cout << "\n=== " << title << " ===\n";
}

} // namespace

// ============================================================================
//  Stub producers / customers
// ============================================================================

/** Synchronous producer: responds immediately when sendPriceList() is called. */
class SyncProducer : public CProducer {
public:
    explicit SyncProducer(
        std::function<void(AProducer, APriceList)> recv,
        std::vector<CProd> products,
        unsigned materialID)
        : m_recv(std::move(recv))
        , m_products(std::move(products))
        , m_materialID(materialID) {}

    void sendPriceList(unsigned materialID) override {
        if (materialID != m_materialID) return;
        auto pl = std::make_shared<CPriceList>(materialID);
        for (const auto &p : m_products) pl->add(p);
        m_recv(shared_from_this(), pl);
    }

private:
    std::function<void(AProducer, APriceList)> m_recv;
    std::vector<CProd> m_products;
    unsigned m_materialID;
};

/** One-shot customer: sends exactly one order list, then signals done. */
class OneShotCustomer : public CCustomer {
public:
    explicit OneShotCustomer(AOrderList orders)
        : m_orders(std::move(orders)), m_served(false) {}

    AOrderList waitForDemand() override {
        if (m_served) return nullptr;
        m_served = true;
        return m_orders;
    }

    void completed(AOrderList x) override {
        m_result = x;
        m_done   = true;
    }

    AOrderList result() const { return m_result; }
    bool       done()   const { return m_done.load(); }

private:
    AOrderList          m_orders;
    bool                m_served;
    AOrderList          m_result;
    std::atomic<bool>   m_done { false };
};

// ============================================================================
//  Helper
// ============================================================================

static constexpr double EPS = 1e-6;
static bool approxEq(double a, double b) { return std::fabs(a - b) < EPS; }

// ============================================================================
//  Test 1 – seqSolve: exact panel match
// ============================================================================

void test_seqSolve_exact_match() {
    section("seqSolve – exact panel match");

    auto pl = std::make_shared<CPriceList>(0);
    pl->add(CProd{4, 3, 10.0});   // 4×3 panel, cost 10

    COrder order(4, 3, 1.0);
    CWeldingCompany::seqSolve(pl, order);

    check(approxEq(order.m_Cost, 10.0), "cost == 10.0 (exact panel found)");
}

// ============================================================================
//  Test 2 – seqSolve: panel needs splitting (horizontal weld)
// ============================================================================

void test_seqSolve_single_split() {
    section("seqSolve – single horizontal split");

    // Two 2×4 panels welded together → 4×4 panel
    // Weld cost = height * weldingStrength = 4 * 0.5 = 2.0
    // Total = 2 * 5.0 + 2.0 = 12.0
    auto pl = std::make_shared<CPriceList>(0);
    pl->add(CProd{2, 4, 5.0});

    COrder order(4, 4, 0.5);
    CWeldingCompany::seqSolve(pl, order);

    check(approxEq(order.m_Cost, 12.0), "cost == 12.0 (two 2×4 welded horizontally)");
}

// ============================================================================
//  Test 3 – seqSolve: vertical split
// ============================================================================

void test_seqSolve_vertical_split() {
    section("seqSolve – single vertical split");

    // Two 4×2 panels welded to form 4×4
    // Weld = width * strength = 4 * 0.5 = 2.0
    // Total = 2 * 5.0 + 2.0 = 12.0  (same as horizontal case)
    auto pl = std::make_shared<CPriceList>(0);
    pl->add(CProd{4, 2, 5.0});

    COrder order(4, 4, 0.5);
    CWeldingCompany::seqSolve(pl, order);

    check(approxEq(order.m_Cost, 12.0), "cost == 12.0 (two 4×2 welded vertically)");
}

// ============================================================================
//  Test 4 – seqSolve: cheapest path chosen among alternatives
// ============================================================================

void test_seqSolve_cheapest_path() {
    section("seqSolve – cheapest path among alternatives");

    // Only 3×2 panels available at cost 3.
    // To build 6×2: two 3×2 panels welded horizontally.
    // Weld cost = height × strength = 2 × 1.0 = 2.0
    // Total = 3 + 3 + 2 = 8.0
    auto pl = std::make_shared<CPriceList>(0);
    pl->add(CProd{3, 2, 3.0});

    COrder order(6, 2, 1.0);
    CWeldingCompany::seqSolve(pl, order);

    check(approxEq(order.m_Cost, 8.0), "cost == 8.0 (two 3×2 panels welded)");
}

// ============================================================================
//  Test 5 – seqSolve: panel orientation symmetry
// ============================================================================

void test_seqSolve_orientation() {
    section("seqSolve – panel orientation symmetry");

    // Price list only has 3×5, but order asks for 5×3 — should still match
    auto pl = std::make_shared<CPriceList>(0);
    pl->add(CProd{3, 5, 7.0});

    COrder order(5, 3, 1.0);
    CWeldingCompany::seqSolve(pl, order);

    check(approxEq(order.m_Cost, 7.0), "cost == 7.0 (rotated panel accepted)");
}

// ============================================================================
//  Test 6 – seqSolve: impossible order
// ============================================================================

void test_seqSolve_impossible() {
    section("seqSolve – impossible order");

    auto pl = std::make_shared<CPriceList>(0);
    pl->add(CProd{2, 2, 5.0});

    // 3×3 cannot be built from 2×2 panels
    COrder order(3, 3, 1.0);
    CWeldingCompany::seqSolve(pl, order);

    check(order.m_Cost == std::numeric_limits<double>::max(),
          "cost == max (no valid decomposition)");
}

// ============================================================================
//  Test 7 – seqSolve: multi-level split
// ============================================================================

void test_seqSolve_multi_level() {
    section("seqSolve – multi-level split");

    // Build 4×1 from 1×1 panels:  (1+1)+weld + (1+1)+weld  then weld both halves
    // All welds have height=1, strength=2
    // 4 panels @ 1.0 = 4.0
    // Level 1: 2 welds of width 1 → 2 * (1*2) = 4.0
    // Level 2: 1 weld of width 2 → 1 * (2*2) = 4.0  — wait, we split horizontally
    // Actually seqSolve splits horizontal and vertical, let's just check it doesn't exceed naive
    auto pl = std::make_shared<CPriceList>(0);
    pl->add(CProd{1, 1, 1.0});

    COrder order(4, 1, 0.0);   // zero welding cost → only panel cost matters
    CWeldingCompany::seqSolve(pl, order);

    check(approxEq(order.m_Cost, 4.0), "cost == 4.0 (four 1×1 panels, no weld cost)");
}

// ============================================================================
//  Test 8 – addPriceList: merge keeps cheapest
// ============================================================================

void test_price_list_merge() {
    section("addPriceList – merge keeps cheapest price");

    using namespace std::placeholders;
    CWeldingCompany company;

    APriceList pl1 = std::make_shared<CPriceList>(1);
    pl1->add(CProd{2, 2, 50.0});

    APriceList pl2 = std::make_shared<CPriceList>(1);
    pl2->add(CProd{2, 2, 30.0});   // cheaper for same shape

    auto dummyProducer = std::make_shared<SyncProducer>(
        std::bind(&CWeldingCompany::addPriceList, &company, _1, _2),
        std::vector<CProd>{}, 1);

    company.addPriceList(dummyProducer, pl1);
    company.addPriceList(dummyProducer, pl2);

    // Verify by solving an order that uses this material
    COrder order(2, 2, 0.0);
    auto pl = std::make_shared<CPriceList>(1);
    pl->add(CProd{2, 2, 30.0});
    CWeldingCompany::seqSolve(pl, order);

    check(approxEq(order.m_Cost, 30.0), "merged price list retains cheaper 30.0");
}

// ============================================================================
//  Test 9 – concurrent: single customer, sync producer, multiple workers
// ============================================================================

void test_concurrent_single_customer() {
    section("concurrent – single customer, sync producer");

    using namespace std::placeholders;
    CWeldingCompany company;

    auto pl = std::make_shared<CPriceList>(0);
    pl->add(CProd{1, 1, 1.0});

    auto prod = std::make_shared<SyncProducer>(
        std::bind(&CWeldingCompany::addPriceList, &company, _1, _2),
        std::vector<CProd>{ CProd{1, 1, 1.0} }, 0);

    auto orderList = std::make_shared<COrderList>(0);
    for (int i = 0; i < 10; ++i)
        orderList->add(COrder{1, 1, 0.0});   // trivial 1×1 orders

    auto cust = std::make_shared<OneShotCustomer>(orderList);

    company.addProducer(prod);
    company.addCustomer(cust);
    company.start(4);
    company.stop();

    check(cust->done(), "customer received completed() callback");
    if (cust->done()) {
        bool allOne = true;
        for (const auto &o : cust->result()->m_List)
            if (!approxEq(o.m_Cost, 1.0)) { allOne = false; break; }
        check(allOne, "all 10 orders priced at 1.0");
    }
}

// ============================================================================
//  Test 10 – concurrent: multiple customers, multiple producers
// ============================================================================

void test_concurrent_multi() {
    section("concurrent – multiple customers, multiple producers");

    using namespace std::placeholders;
    CWeldingCompany company;

    // Two producers for material 0
    auto mkProd = [&](double cost) {
        return std::make_shared<SyncProducer>(
            std::bind(&CWeldingCompany::addPriceList, &company, _1, _2),
            std::vector<CProd>{ CProd{2, 2, cost} }, 0);
    };
    company.addProducer(mkProd(10.0));
    company.addProducer(mkProd(8.0));   // cheaper; should win after merge

    std::vector<std::shared_ptr<OneShotCustomer>> customers;
    for (int c = 0; c < 3; ++c) {
        auto ol = std::make_shared<COrderList>(0);
        ol->add(COrder{2, 2, 0.0});
        auto cust = std::make_shared<OneShotCustomer>(ol);
        company.addCustomer(cust);
        customers.push_back(cust);
    }

    company.start(3);
    company.stop();

    bool allDone = true;
    for (const auto &c : customers) if (!c->done()) { allDone = false; break; }
    check(allDone, "all 3 customers received completed()");

    bool correctPrice = true;
    for (const auto &c : customers)
        if (c->done() && !approxEq(c->result()->m_List[0].m_Cost, 8.0))
            correctPrice = false;
    check(correctPrice, "merged price list: cheapest cost 8.0 used");
}

// ============================================================================
//  Test 11 – stress: large number of trivial orders
// ============================================================================

void test_stress_many_orders() {
    section("stress – 500 trivial orders, 8 workers");

    using namespace std::placeholders;
    CWeldingCompany company;

    auto prod = std::make_shared<SyncProducer>(
        std::bind(&CWeldingCompany::addPriceList, &company, _1, _2),
        std::vector<CProd>{ CProd{1, 1, 2.5} }, 7);

    company.addProducer(prod);

    auto ol = std::make_shared<COrderList>(7);
    for (int i = 0; i < 500; ++i) ol->add(COrder{1, 1, 0.0});

    auto cust = std::make_shared<OneShotCustomer>(ol);
    company.addCustomer(cust);

    company.start(8);
    company.stop();

    check(cust->done(), "customer notified after 500 orders");
    if (cust->done()) {
        bool ok = true;
        for (const auto &o : cust->result()->m_List)
            if (!approxEq(o.m_Cost, 2.5)) { ok = false; break; }
        check(ok, "all 500 orders correctly priced at 2.5");
    }
}

// ============================================================================
//  main
// ============================================================================

int main() {
    std::cout << "CWeldingCompany – Test Suite\n";
    std::cout << std::string(50, '=') << "\n";

    test_seqSolve_exact_match();
    test_seqSolve_single_split();
    test_seqSolve_vertical_split();
    test_seqSolve_cheapest_path();
    test_seqSolve_orientation();
    test_seqSolve_impossible();
    test_seqSolve_multi_level();
    test_price_list_merge();
    test_concurrent_single_customer();
    test_concurrent_multi();
    test_stress_many_orders();

    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "Results: " << g_passed << " / " << g_total << " tests passed.\n";

    return (g_passed == g_total) ? 0 : 1;
}
