#pragma once

struct ncplane;

namespace why {

class DspEngine;

class Visualizer {
public:
    virtual ~Visualizer() = default;
    virtual void render(struct ncplane* plane, const DspEngine& dsp) = 0;
};

} // namespace why
