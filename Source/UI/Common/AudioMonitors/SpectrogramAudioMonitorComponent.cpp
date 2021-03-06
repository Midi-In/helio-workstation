/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "SpectrogramAudioMonitorComponent.h"
#include "ComponentFader.h"
#include "AudioCore.h"
#include "ColourIDs.h"

static const float kPeakSpectrumFrequencies[] =
{
    63.f,
    125.f, 200.f,
    315.f, 500.f,
    800.f, 1250.f,
    2000.f, 3150.f,
    5000.f, 8000.f
};

SpectrogramAudioMonitorComponent::SpectrogramAudioMonitorComponent(WeakReference<AudioMonitor> monitor)
    : Thread("GenericAudioMonitor"),
    colour(findDefaultColour(ColourIDs::AudioMonitor::foreground)),
    audioMonitor(monitor)
{
    // (true, false) will enable switching rendering modes on click
    this->setInterceptsMouseClicks(false, false);
    this->setPaintingIsUnclipped(true);

    for (int band = 0; band < SpectrogramAudioMonitorComponent::numBands; ++band)
    {
        this->bands.add(new SpectrumBand());
    }
    
    this->lPeakBand = make<SpectrumBand>();
    this->rPeakBand = make<SpectrumBand>();

    if (this->audioMonitor != nullptr)
    {
        this->startThread(5);
    }
}

void SpectrogramAudioMonitorComponent::setTargetAnalyzer(WeakReference<AudioMonitor> monitor)
{
    if (monitor != nullptr)
    {
        this->audioMonitor = monitor;
        this->startThread(5);
    }
}

SpectrogramAudioMonitorComponent::~SpectrogramAudioMonitorComponent()
{ 
    this->stopThread(1000);
}

void SpectrogramAudioMonitorComponent::run()
{
    while (! this->threadShouldExit())
    {
        Thread::sleep(jlimit(10, 100, 35 - this->skewTime));
        const double b = Time::getMillisecondCounterHiRes();

        if (this->isVisible())
        {
            for (int i = 0; i < SpectrogramAudioMonitorComponent::numBands; ++i)
            {
                this->lPeak = this->audioMonitor->getPeak(0);
                this->rPeak = this->audioMonitor->getPeak(1);
                this->values[i] = this->audioMonitor->getInterpolatedSpectrumAtFrequency(kPeakSpectrumFrequencies[i]);
            }

            this->triggerAsyncUpdate();
        }

        const double a = Time::getMillisecondCounterHiRes();
        this->skewTime = int(a - b);
    }
}

void SpectrogramAudioMonitorComponent::handleAsyncUpdate()
{
    this->repaint();
}

void SpectrogramAudioMonitorComponent::resized()
{
    for (int i = 0; i < this->bands.size(); ++i)
    {
        this->bands[i]->reset();
    }
}

void SpectrogramAudioMonitorComponent::paint(Graphics &g)
{
    if (this->audioMonitor == nullptr)
    {
        return;
    }
    
    const float bw = 4.f;
    const float w = float(this->getWidth());
    const float h = float(this->getHeight());
    const uint32 timeNow = Time::getMillisecondCounter();

    this->lPeakBand->processSignal(this->lPeak.get(), h, timeNow);
    this->rPeakBand->processSignal(this->rPeak.get(), h, timeNow);

    // Volume indicators:
    // TODO pretty up their look and add yellow/red colors for clipped signal

    //g.setColour(Colours::white.withAlpha(0.025f));
    //g.fillRect(0.f, h - this->lPeakBand->value, w / 2.f, this->lPeakBand->value);
    //g.fillRect(w / 2.f, h - this->rPeakBand->value, w / 2.f, this->rPeakBand->value);
    ////g.drawHorizontalLine(h - this->lPeakBand->value, 0.f, w / 2.f);
    ////g.drawHorizontalLine(h - this->rPeakBand->value, w / 2.f, w);

    // Volume peaks:

    const float lAlpha = (0.375f - this->lPeakBand->peakDecayColour) / 12.f;
    const float yPeakL = (h - this->lPeakBand->peak - 1.f);
    g.setColour(this->colour.withAlpha(lAlpha));
    g.fillRect(0.f, yPeakL, w / 2.f - 1.f, this->lPeakBand->peak);
    g.fillRect(0.f, yPeakL, w / 2.f - 1.f, 1.f);

    const float rAlpha = (0.375f - this->rPeakBand->peakDecayColour) / 12.f;
    const float yPeakR = (h - this->rPeakBand->peak - 1.f);
    g.setColour(this->colour.withAlpha(rAlpha));
    g.fillRect(w / 2.f, yPeakR, w / 2.f - 1.f, this->rPeakBand->peak);
    g.fillRect(w / 2.f, yPeakR, w / 2.f - 1.f, 1.f);

    // Spectrum bands:

    for (int i = 0; i < SpectrogramAudioMonitorComponent::numBands; ++i)
    {
        this->bands[i]->processSignal(this->values[i].get(), h, timeNow);
    }

    g.setColour(this->colour.withAlpha(0.35f));

    for (int i = 0; i < SpectrogramAudioMonitorComponent::numBands; ++i)
    {
        const float x = i * bw + 1.f;
        for (int j = int(h + 1); j > (h - this->bands[i]->value); j -= 2)
        {
            g.fillRect(x, float(j), bw - 1.f, 1.f);
        }
    }

    for (int i = 0; i < SpectrogramAudioMonitorComponent::numBands; ++i)
    {
        const float x = i * bw + 1.f;
        g.setColour(this->colour.withAlpha(0.4f - this->bands[i]->peakDecayColour));
        const float peakH = (h - this->bands[i]->peak - 2.f);
        g.fillRect(x, peakH, bw - 1.f, 1.f);
    }

    // Show levels?
    //if (this->altMode)
    //{
    //    const int valueForMinus6dB = AudioCore::iecLevel(-6.f) * this->getHeight();
    //    const int valueForMinus12dB = AudioCore::iecLevel(-12.f) * this->getHeight();
    //    const int valueForMinus24dB = AudioCore::iecLevel(-24.f) * this->getHeight();
    //    
    //    g.setColour(this->colour.withAlpha(0.075f));
    //    g.drawHorizontalLine(height - valueForMinus6dB, 0.f, this->getWidth());
    //    g.drawHorizontalLine(height - valueForMinus12dB, 0.f, this->getWidth());
    //    g.drawHorizontalLine(height - valueForMinus24dB, 0.f, this->getWidth());
    //}
}

void SpectrogramAudioMonitorComponent::SpectrumBand::reset()
{
    this->value = 0.f;
    this->valueDecay = 1.f;
    this->peak = 0.f;
    this->peakDecay = 1.f;
    this->peakDecayColour = SpectrumBand::maxAlpha;
}

inline void SpectrogramAudioMonitorComponent::SpectrumBand::processSignal(float signal, float h, uint32 timeNow)
{
    static constexpr auto maxDb = +4.0f;
    static constexpr auto minDb = -70.0f;

    const float valueInDb = jlimit(minDb, maxDb,
        20.f * AudioCore::fastLog10(signal));

    const float valueInY = AudioCore::iecLevel(valueInDb) * h;

    if (this->value < valueInY)
    {
        this->value = valueInY;
        this->valueDecay = 0.f;
        this->valueDecayStart = Time::getMillisecondCounter();
    }
    else
    {
        const auto msElapsed = int(timeNow - this->valueDecayStart);
        float newProgress = msElapsed / float(SpectrumBand::bandFadeMs);
        if (newProgress >= 0.f && newProgress < 1.f)
        {
            newProgress = ComponentFader::timeToDistance(newProgress);
            jassert(newProgress >= this->valueDecay);
            const float delta = (newProgress - this->valueDecay) / (1.f - this->valueDecay);
            this->valueDecay = newProgress;
            this->value -= (this->value * delta);
        }
        else
        {
            this->value = 0.f;
        }
    }

    if (this->peak < this->value)
    {
        this->peak = this->value;
        this->peakDecay = 0.f;
        this->peakDecayStart = Time::getMillisecondCounter();
    }
    else
    {
        const auto msElapsed = int(timeNow - this->peakDecayStart);
        float newProgress = msElapsed / float(SpectrumBand::peakFadeMs);
        if (newProgress >= 0.f && newProgress < 1.f)
        {
            newProgress = ComponentFader::timeToDistance(newProgress);
            jassert(newProgress >= this->peakDecay);
            const float delta = (newProgress - this->peakDecay) / (1.f - this->peakDecay);
            this->peakDecayColour = (newProgress * newProgress * newProgress) * SpectrumBand::maxAlpha;
            this->peakDecay = newProgress;
            this->peak -= (this->peak * delta);
        }
        else
        {
            this->peakDecayColour = SpectrumBand::maxAlpha;
            this->peak = 0.f;
        }
    }
}
