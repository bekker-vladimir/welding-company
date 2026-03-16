// The classes in this header are used in the example test. You are free to
// modify these classes, add more test cases, and add more test sets.
// These classes do not exist in the progtest's testing environment.
#ifndef SAMPLE_TESTER_H_90245724659246123418345
#define SAMPLE_TESTER_H_90245724659246123418345

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "common.h"

class CProducerSync : public CProducer {
public:
    CProducerSync(std::function<void(AProducer, APriceList)> receiver)
        : m_Receiver(receiver) {
    }

    virtual void sendPriceList(unsigned materialID) override;

private:
    static std::vector<CProd> c_Prod;
    std::function<void(AProducer, APriceList)> m_Receiver;
};

class CProducerAsync : public CProducer {
public:
    CProducerAsync(std::function<void(AProducer, APriceList)> receiver)
        : m_Receiver(receiver) {
    }

    virtual void sendPriceList(unsigned materialID) override;

    void start();

    void stop();

private:
    static std::vector<CProd> c_Prod;
    std::function<void(AProducer, APriceList)> m_Receiver;
    std::thread m_Thr;
    std::mutex m_Mtx;
    std::condition_variable m_Cond;
    unsigned m_Req = 0;
    bool m_Stop = false;

    void prodThr();
};

using AProducerAsync = std::shared_ptr<CProducerAsync>;

class CCustomerTest : public CCustomer {
public:
    CCustomerTest(unsigned count)
        : m_Count(count) {
    }

    virtual AOrderList waitForDemand();

    virtual void completed(AOrderList x);

private:
    static std::vector<std::pair<COrder, double> > c_Orders;
    unsigned m_Count;
};
#endif /* SAMPLE_TESTER_H_90245724659246123418345 */
