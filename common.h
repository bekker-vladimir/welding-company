// The classes in this header define the common interface between your implementation and
// the testing environment. Exactly the same implementation is present in the progtest's
// testing environment. You are not supposed to modify any declaration in this file,
// any change is likely to break the compilation.
#ifndef COMMON_H_09824352756248526345245
#define COMMON_H_09824352756248526345245

#include <vector>
#include <memory>

//=============================================================================================================================================================
class CProd {
public:
    CProd(unsigned w,
          unsigned h,
          double cost)
        : m_W(w),
          m_H(h),
          m_Cost(cost) {
    }

    unsigned m_W;
    unsigned m_H;
    double m_Cost;
};

//=============================================================================================================================================================
// 1 priceList == 1 dodavatel, 1 material, n produktu

class CPriceList {
public:
    CPriceList(unsigned materialID)
        : m_MaterialID(materialID) {
    }

    virtual ~CPriceList() = default;

    CPriceList *add(const CProd &x) {
        m_List.push_back(x);
        return this;
    }

    unsigned m_MaterialID;
    std::vector<CProd> m_List;
};

using APriceList = std::shared_ptr<CPriceList>;

//=============================================================================================================================================================
class COrder {
public:
    COrder(unsigned w,
           unsigned h,
           double weldingStrength)
        : m_W(w),
          m_H(h),
          m_WeldingStrength(weldingStrength) {
    }

    unsigned m_W;
    unsigned m_H;
    double m_WeldingStrength;
    double m_Cost = 0;
};

//=============================================================================================================================================================
class COrderList {
public:
    COrderList(unsigned materialID)
        : m_MaterialID(materialID) {
    }

    virtual ~COrderList() = default;

    COrderList *add(const COrder &x) {
        m_List.push_back(x);
        return this;
    }

    unsigned m_MaterialID;
    std::vector<COrder> m_List;
};

using AOrderList = std::shared_ptr<COrderList>;

//=============================================================================================================================================================
class CProducer : public std::enable_shared_from_this<CProducer> {
public:
    virtual ~CProducer() = default;

    virtual void sendPriceList(unsigned materialID) = 0;
};

using AProducer = std::shared_ptr<CProducer>;

//=============================================================================================================================================================
class CCustomer : public std::enable_shared_from_this<CCustomer> {
public:
    virtual ~CCustomer() = default;

    virtual AOrderList waitForDemand() = 0;

    virtual void completed(AOrderList x) = 0;
};

using ACustomer = std::shared_ptr<CCustomer>;
//=============================================================================================================================================================
#endif /* COMMON_H_09824352756248526345245 */
