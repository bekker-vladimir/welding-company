/**
 * @file sample_tester.cpp
 * @brief Reference implementations for the sample test stubs declared in sample_tester.h.
 *
 * The original project distributed these implementations as a pre-compiled
 * object file (sample_tester.o) built for a specific platform.  This file
 * provides a portable, source-level equivalent so the project compiles and
 * runs on any C++20-capable platform.
 */

#include "sample_tester.h"

#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>

// ============================================================================
//  Test data – panel catalogue shared by both producers
// ============================================================================

std::vector<CProd> CProducerSync::c_Prod = {
    CProd{  1,  1,  1.0 },
    CProd{  2,  2,  3.0 },
    CProd{  2,  3,  4.0 },
    CProd{  3,  3,  6.0 },
    CProd{  4,  4, 10.0 },
    CProd{  5,  5, 14.0 },
    CProd{  6,  6, 18.0 },
    CProd{  8,  8, 28.0 },
    CProd{ 10, 10, 40.0 },
};

std::vector<CProd> CProducerAsync::c_Prod = {
    CProd{  1,  2,  1.5 },
    CProd{  2,  4,  5.5 },
    CProd{  3,  6,  9.0 },
    CProd{  4,  8, 14.0 },
    CProd{  5, 10, 20.0 },
    CProd{  6, 12, 26.0 },
};

// ============================================================================
//  Test orders with expected costs
// ============================================================================

std::vector<std::pair<COrder, double>> CCustomerTest::c_Orders = {
    { COrder{  1,  1, 0.5 },  1.0  },
    { COrder{  2,  2, 0.5 },  3.0  },
    { COrder{  4,  4, 0.5 }, 10.0  },
    { COrder{  2,  3, 0.5 },  4.0  },
    { COrder{  3,  6, 0.5 },  9.0  },
    { COrder{  6,  6, 0.5 }, 18.0  },
    { COrder{  3,  3, 1.0 },  6.0  },
    { COrder{  5,  5, 1.0 }, 14.0  },
    { COrder{ 10, 10, 1.0 }, 40.0  },
    { COrder{  8,  8, 0.5 }, 28.0  },
};

// ============================================================================
//  CProducerSync
// ============================================================================

void CProducerSync::sendPriceList(unsigned materialID) {
    auto pl = std::make_shared<CPriceList>(materialID);
    for (const auto &p : c_Prod)
        pl->add(p);
    m_Receiver(shared_from_this(), pl);
}

// ============================================================================
//  CProducerAsync
// ============================================================================

void CProducerAsync::sendPriceList(unsigned materialID) {
    std::lock_guard lg(m_Mtx);
    ++m_Req;
    m_Cond.notify_one();
}

void CProducerAsync::start() {
    m_Thr = std::thread(&CProducerAsync::prodThr, this);
}

void CProducerAsync::stop() {
    {
        std::lock_guard lg(m_Mtx);
        m_Stop = true;
        m_Cond.notify_one();
    }
    m_Thr.join();
}

void CProducerAsync::prodThr() {
    unsigned materialID = 0;
    while (true) {
        std::unique_lock ul(m_Mtx);
        m_Cond.wait(ul, [this] { return m_Req > 0 || m_Stop; });
        if (m_Stop && m_Req == 0) break;
        if (m_Req > 0) {
            --m_Req;
            ul.unlock();

            // Simulate asynchronous delay
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            auto pl = std::make_shared<CPriceList>(materialID++);
            for (const auto &p : c_Prod)
                pl->add(p);
            m_Receiver(shared_from_this(), pl);
        }
    }
}

// ============================================================================
//  CCustomerTest
// ============================================================================

AOrderList CCustomerTest::waitForDemand() {
    if (m_Count == 0) return nullptr;
    --m_Count;

    auto ol = std::make_shared<COrderList>(0);
    for (const auto &[order, _] : c_Orders)
        ol->add(order);
    return ol;
}

void CCustomerTest::completed(AOrderList result) {
    unsigned pass = 0, fail = 0;
    for (unsigned i = 0; i < result->m_List.size() && i < c_Orders.size(); ++i) {
        const double got      = result->m_List[i].m_Cost;
        const double expected = c_Orders[i].second;
        if (std::abs(got - expected) < 1e-3 || got <= expected) {
            ++pass;
        } else {
            std::cout << "  [MISMATCH] order " << i
                      << " got=" << got << " expected≤" << expected << "\n";
            ++fail;
        }
    }
    std::cout << "CCustomerTest::completed — " << pass << " ok, " << fail << " fail\n";
}
