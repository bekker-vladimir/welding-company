#include <cstdlib>
#include <functional>
#include <memory>

#include "../include/CWeldingCompany.h"
#include "../sample_tester.h"

int main(){
    using namespace std::placeholders;

    CWeldingCompany company;

    const AProducer p1 = std::make_shared<CProducerSync>(
        std::bind(&CWeldingCompany::addPriceList, &company, _1, _2));
    auto p2 = std::make_shared<CProducerAsync>(
        std::bind(&CWeldingCompany::addPriceList, &company, _1, _2));

    company.addProducer(p1);
    company.addProducer(p2);
    company.addCustomer(std::make_shared<CCustomerTest>(2));

    p2->start();
    company.start(3);
    company.stop();
    p2->stop();

    return EXIT_SUCCESS;
}
