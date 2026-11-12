#pragma once

#include "fx/FxGraph.h"

#include <memory>
#include <string>
#include <vector>

namespace andor::analog::fx
{
class Processor;
struct Impulse;   // (declared as it is defined: MSVC's names tell class and struct apart)
}

namespace andor::modular
{

/**
    A module that plays one processor of ANDOR ANALOG's FX library (fx.phaser, fx.compressor...):
    what a rack reads of the running module beyond its parameters. The processor keeps its own
    meters as atomics, so a pointer read on the message thread is safe for as long as the
    compiled graph that owns the module lives (the engine frees graphs on the message thread).
*/
class AnalogFxAccess
{
public:
    virtual ~AnalogFxAccess() = default;
    virtual const andor::analog::fx::Processor* analogFx() const noexcept = 0;
    /** CONVOLUTION: the impulse it plays (null: any other effect). */
    virtual std::shared_ptr<const andor::analog::fx::Impulse> analogImpulse() const { return nullptr; }
};

/** How a module's parameter carries one setting of the FX library's table
    (andor::analog::fx::parameterSpecs): the setting's value is the parameter's times `scale`
    (a percentage is 0..1 here and 0..100 there). */
struct AnalogFxLink
{
    std::string setting;   ///< the table's name ("fet.input")
    std::string param;     ///< this module's parameter id
    double scale = 1.0;
};

/** The library effect a module type plays; false for any other module. */
bool analogFxType (const std::string& nodeType, andor::analog::fx::Type& type);
/** Every setting of the effect and the parameter that carries it (all but ON: a module's own
    light is its bypass). Empty for any other module. */
const std::vector<AnalogFxLink>& analogFxLinks (const std::string& nodeType);

} // namespace andor::modular
