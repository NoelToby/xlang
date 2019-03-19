#include "pch.h"
#include "winrt/test_component_base.h"
#include "winrt/test_component_derived.nested.h"
#include <inspectable.h>
#include <weakreference.h>

using namespace winrt;
using namespace test_component_base;
using namespace test_component_derived::Nested;

namespace
{
    struct HierarchyE : HierarchyDT<HierarchyE>
    {
    };
}

TEST_CASE("hierarchy")
{
    HierarchyA a;
    REQUIRE(a == weak_ref<HierarchyA>(a).get()
        .as<IUnknown>()
        .as<IInspectable>()
        .as<IWeakReferenceSource>()
        .as<IAgileObject>()
        .as<IMarshal>()
        .as<HierarchyA>());

    HierarchyB b;
    REQUIRE(b == weak_ref<HierarchyB>(b).get()
        .as<IUnknown>()
        .as<IInspectable>()
        .as<IWeakReferenceSource>()
        .as<IAgileObject>()
        .as<IMarshal>()
        .as<HierarchyA>()
        .as<HierarchyB>());

    HierarchyC c;
    REQUIRE(c == weak_ref<HierarchyC>(c).get()
        .as<IUnknown>()
        .as<IInspectable>()
        .as<IWeakReferenceSource>()
        .as<IAgileObject>()
        .as<IMarshal>()
        .as<HierarchyA>()
        .as<HierarchyB>()
        .as<HierarchyC>());

    HierarchyD d;
    REQUIRE(d == weak_ref<HierarchyD>(d).get()
        .as<IUnknown>()
        .as<IInspectable>()
        .as<IWeakReferenceSource>()
        .as<IAgileObject>()
        .as<IMarshal>()
        .as<HierarchyA>()
        .as<HierarchyB>()
        .as<HierarchyC>()
        .as<HierarchyD>());

    auto e = make<HierarchyE>();

    REQUIRE(e.as<IUnknown>() == weak_ref<HierarchyD>(e).get()
        .as<IUnknown>()
        .as<IInspectable>()
        .as<IWeakReferenceSource>()
        .as<IAgileObject>()
        .as<IMarshal>()
        .as<HierarchyA>()
        .as<HierarchyB>()
        .as<HierarchyC>()
        .as<HierarchyD>()
        .as<IUnknown>());
}

TEST_CASE("hierarchy_fail")
{
    HierarchyD d;
    REQUIRE(d.try_as<IWeakReference>() == nullptr);
}
