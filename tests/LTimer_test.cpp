#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>

#include "LTimer.hpp"

using namespace std::chrono_literals;

TEST_CASE("LTimer measures and pauses elapsed time", "[LTimer]") {
    LTimer timer;
    LTimer::sleep_for(1ms);

    REQUIRE(timer.ns() > 0);
    REQUIRE(timer.expired(0ns));

    timer.stop();
    const auto stopped = timer.ns();
    LTimer::sleep_for(1ms);
    REQUIRE(timer.ns() == stopped);

    timer.resume();
    LTimer::sleep_for(1ms);
    REQUIRE(timer.ns() >= stopped);
}

TEST_CASE("LScopeTimer invokes callback once", "[LTimer]") {
    int called = 0;
    {
        LScopeTimer timer([&](const LTimer& value) {
            REQUIRE(value.ns() >= 0);
            ++called;
        });
    }
    REQUIRE(called == 1);
}

TEST_CASE("LScopeTimer never propagates a callback exception from destruction", "[LTimer]") {
    int called = 0;
    {
        LScopeTimer timer([&](const LTimer&) {
            ++called;
            throw std::runtime_error("expected");
        });
    }
    REQUIRE(called == 1);
}
