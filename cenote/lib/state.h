#pragma once

#ifndef H_STATE_H
#define H_STATE_H

#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

namespace daisysp
{

struct TerrariumState {
    float pot1;
    float pot2;
    float pot3;
    float pot4;
    float pot5;
    float pot6;

    bool sw1, sw2, sw3, sw4;

    TerrariumState() 
        : pot1(0.0f), 
          pot2(0.0f), 
          pot3(0.0f), 
          pot4(0.0f), 
          pot5(0.0f), 
          pot6(0.0f),
          sw1(false), 
          sw2(false), 
          sw3(false), 
          sw4(false) 
    {}
};
 

} // namespace daisysp



#endif // H_STATE_H
