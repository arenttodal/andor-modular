#include "TestUtil.h"

#include "core/Json.h"
#include "core/Prng.h"

using namespace andor::modular;

TEST_CASE ("JSON round-trips values and escapes", "[unit][json]")
{
    auto v = json::Value::object();
    v.set ("name", "a \"quoted\"\nline \xE2\x99\xAA");
    v.set ("n", 0.1);
    v.set ("big", 123456789.0);
    v.set ("neg", -2.5e-7);
    v.set ("flag", true);
    auto arr = json::Value::array();
    arr.push (1);
    arr.push (json::Value());
    v.set ("arr", arr);
    const auto text = json::write (v, 2);
    const auto back = json::parse (text);
    REQUIRE (back.ok);
    CHECK (back.value.getString ("name") == v.getString ("name"));
    CHECK (back.value.get ("n", 0) == 0.1);
    CHECK (back.value.get ("neg", 0) == -2.5e-7);
    CHECK (back.value["arr"][1].isNull());
    CHECK (json::write (back.value, 2) == text);   // deterministic
    CHECK (json::parse ("\"\\u00e9\\ud83c\\udfb9\"").value.asString() == "\xC3\xA9\xF0\x9F\x8E\xB9");
}

TEST_CASE ("JSON parser refuses hostile input without crashing", "[unit][json][fuzz]")
{
    CHECK_FALSE (json::parse ("").ok);
    CHECK_FALSE (json::parse ("{").ok);
    CHECK_FALSE (json::parse ("[1,]").ok);
    CHECK_FALSE (json::parse ("{\"a\":1} x").ok);
    CHECK_FALSE (json::parse ("1e999").ok);
    CHECK_FALSE (json::parse (std::string (100, '[') + std::string (100, ']')).ok);   // too deep
    CHECK (json::parse (std::string (60, '[') + std::string (60, ']')).ok);

    // Random mutations of a real patch: every parse either fails cleanly or loads.
    Document d = test::basicPatch();
    const auto good = saveToString (d);
    osp::Prng r (42);
    for (int i = 0; i < 3000; ++i)
    {
        std::string t = good;
        const int edits = 1 + static_cast<int> (r.nextBelow (6));
        for (int e = 0; e < edits && ! t.empty(); ++e)
        {
            const auto at = static_cast<std::size_t> (r.nextBelow (t.size()));
            switch (r.nextBelow (4))
            {
                case 0: t[at] = static_cast<char> (r.nextBelow (256)); break;
                case 1: t.erase (at, 1 + r.nextBelow (20)); break;
                case 2: t.insert (at, 1, "{}[]\":,0-e\\"[r.nextBelow (11)]); break;
                default: t = t.substr (0, at); break;
            }
        }
        Document out;
        const auto res = loadFromString (t, out);
        if (res.ok)
            CHECK (out.nodes.size() <= 512);
    }
}
