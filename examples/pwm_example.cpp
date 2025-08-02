
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#include <mapper/mapper_cpp.h>

#include "pwm_synth/pwm.h"

using namespace mapper;

int done = 0;

void ctrlc(int)
{
    done = 1;
}

// use simple scalar handler
void handler_freq(Object&& obj, Object::Status status)
{
    Signal sig(obj);
    const void *value = sig.value();
    if (value) {
        set_freq(*(float*)value);
    }
}

void handler_gain(Object&& obj, Object::Status status)
{
    Signal sig(obj);
    const void *value = sig.value();
    if (value) {
        set_gain(*(float*)value);
    }
    else {
        set_gain(0);
    }
}

void handler_duty(Object&& obj, Object::Status status)
{
    Signal sig(obj);
    const void *value = sig.value();
    if (value) {
        set_duty(*(float*)value);
    }
}

int main()
{
    signal(SIGINT, ctrlc);

    Device dev("pwm");

    float min0 = 0;
    float max1 = 1;
    float max1000 = 1000;

    dev.add_signal(Direction::INCOMING, "/freq", 1, Type::FLOAT, "Hz", &min0, &max1000, NULL)
       .add_callback(handler_freq, Object::Status::UPDATE_REM);
    dev.add_signal(Direction::INCOMING, "/gain", 1, Type::FLOAT, "Hz", &min0, &max1, NULL)
       .add_callback(handler_gain, Object::Status::UPDATE_REM);
    dev.add_signal(Direction::INCOMING, "/duty", 1, Type::FLOAT, "Hz", &min0, &max1, NULL)
       .add_callback(handler_duty, Object::Status::UPDATE_REM);

    run_synth();

    set_duty(0.1);
    set_freq(110.0);
    set_gain(0.1);

    printf("Press Ctrl-C to quit.\n");

    while (!done)
        dev.poll(10);

    set_freq(0);
    sleep(1);

    stop_synth();
}
