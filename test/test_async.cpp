#include <Async.h>
#include <cassert>
#include <iostream>

namespace {
unsigned long now = 100;
void tickAt(Async& timers, unsigned long time) { now = time; timers.tick(); }

void periodicTasks() {
    now = 100;
    Async timers;
    int polls = 0, frames = 0, reports = 0;
    bool warming = true;
    timers.delay(30, [&] { warming = false; });
    timers.repeat(5, [&] { ++polls; });
    timers.repeat(20, [&] { ++frames; });
    timers.repeat(100, [&] { ++reports; });
    tickAt(timers, 104);
    assert(polls == 0 && frames == 0 && reports == 0 && warming);
    tickAt(timers, 105);
    assert(polls == 1 && frames == 0);
    tickAt(timers, 120);
    assert(polls == 2 && frames == 1 && warming);
    tickAt(timers, 130);
    assert(!warming && timers.size() == 3);
    // A delayed loop renders the current frame once, rather than replaying a backlog.
    tickAt(timers, 500);
    assert(polls == 4 && frames == 2 && reports == 1);
}

void saveAfterLastEdit() {
    now = 100;
    Async timers;
    Job save;
    int writes = 0;
    auto edited = [&] {
        save.breakFn();
        save = timers.delay(2000, [&] { ++writes; });
    };
    edited();
    now = 300; edited();
    now = 600; edited();
    assert(timers.size() == 1);
    tickAt(timers, 2599);
    assert(writes == 0);
    tickAt(timers, 2600);
    assert(writes == 1 && timers.size() == 0);
    tickAt(timers, 6000);
    assert(writes == 1);
    edited();
    save.breakFn(); // Explicit save / full reset cancels the pending write.
    tickAt(timers, 9000);
    assert(writes == 1);
}

void retryFromSaveCallback() {
    now = 100;
    Async timers;
    Job save;
    int attempts = 0;
    std::function<void()> schedule;
    schedule = [&] {
        save.breakFn();
        save = timers.delay(2000, [&] {
            save.breakFn();
            if (++attempts == 1) schedule();
        });
    };
    schedule();
    tickAt(timers, 2100);
    assert(attempts == 1 && timers.size() == 1);
    tickAt(timers, 4099);
    assert(attempts == 1);
    tickAt(timers, 4100);
    assert(attempts == 2 && timers.size() == 0);
}

void restartAndStepTestCancellation() {
    now = 100;
    Async timers;
    Job restart, steps;
    int reboots = 0, step = 0;
    restart = timers.delay(2000, [&] { ++reboots; });
    now = 200;
    restart.breakFn();
    restart = timers.delay(3000, [&] { ++reboots; });
    steps = timers.repeat(1000, [&] {
        if (++step == 3) { step = -1; steps.breakFn(); }
    });
    tickAt(timers, 1200);
    assert(step == 1 && reboots == 0);
    tickAt(timers, 2200);
    assert(step == 2 && reboots == 0);
    tickAt(timers, 3200);
    assert(step == -1 && reboots == 1 && timers.size() == 0);
    tickAt(timers, 5000);
    assert(step == -1 && reboots == 1);
}
}

unsigned long millis() { return now; }

int main() {
    periodicTasks(); saveAfterLastEdit(); retryFromSaveCallback(); restartAndStepTestCancellation();
    std::cout << "PASS: subjectLab Async periods, delayed loop, save debounce/cancellation,\n"
                 "retry from callback, restart replacement, step test cancellation\n";
}
