#pragma once

#include "IlionVisual.h"

#include <cmath>

// Reading of the measurements IlionVisual makes between a reference and another still capture, in plain terms.
// Nothing is measured here and nothing here knows about benchmarks: it only sorts the numbers that already exist
// into a few cautious categories, so the menu can say what they mean and, above all, what they don't.
//
// The limits below are working conventions for telling "about the same" from "clearly different". They are not a
// validated scale of visual quality, and no score is made out of them.
//
// What this never does: call one capture better than another. A high SSIM is similar structure, a high PSNR is
// little difference of signal, stronger edges may be sharpness or halos, and two still pictures of a game differ
// by themselves (light, water, foliage, particles, temporal antialiasing). Without a native picture as reference
// the numbers only tell how much two captures differ.
class IlionVisualInterpret
{
  public:
    enum class Similarity
    {
        Identical,
        VeryClose,
        Close,
        Different,
        VeryDifferent,
    };

    // Where the differences are, over the grid of areas the measurement keeps
    enum class Location
    {
        None,         // nothing worth pointing at
        Concentrated, // one area well above the rest
        Spread,       // all over the picture
    };

    enum class Edges
    {
        Similar,
        Sharper,
        SharperWithOvershoot, // stronger edges and overshoot next to them: sharpening or halos read like this
        Softer,
    };

    enum class Verdict
    {
        Inconclusive, // the captures don't allow telling the upscaler apart from everything else
        Equivalent,   // about the same in these still captures
        Detectable,   // a difference that can be seen in these still captures
    };

    // What is known about the two captures besides their pixels
    struct Conditions
    {
        bool nativeReference = false; // the reference is a native picture without upscaler
        bool frameGeneration = false; // frame generation was on in one of them
        bool differentGame = false;
        bool hdrReduced = false; // HDR10 values reduced to 8 bit
        // How much captures of one same version differ here, as 1 - SSIM. Negative when it isn't known.
        double sameVersionNoise = -1.0;
    };

    struct Reading
    {
        bool measurable = false;

        Similarity similarity = Similarity::VeryDifferent;

        Location location = Location::None;
        double concentration = 0.0; // most different area against the mean of all areas

        Edges edges = Edges::Similar;
        bool edgesLost = false; // a visible part of the edges of the reference is weaker

        // Most of the pixels that differ do so by very little: light, water, foliage, particles or what changes
        // from one frame to the next read like this
        bool manySmallDifferences = false;

        bool withinSameVersionNoise = false;

        Verdict verdict = Verdict::Inconclusive;

        // Why it is inconclusive
        bool cameraMoved = false;
        bool sceneChanged = false;
        bool generatedFrame = false;
        bool differentGame = false;

        // "Closer to the native picture" can be said. Never "better".
        bool comparableToNative = false;
    };

    static constexpr double SsimVeryClose = 0.98;
    static constexpr double SsimClose = 0.95;
    static constexpr double SsimDifferent = 0.85;
    static constexpr double ClearlyVeryClose = 1.0;  // % of pixels that clearly differ
    static constexpr double ConcentratedRatio = 2.5; // most different area against the mean
    static constexpr double MeanWorthLocating = 0.05; // levels of 255
    static constexpr double EdgeChange = 5.0;          // %
    static constexpr double OvershootSeen = 1.0;       // %
    static constexpr double EdgesKept = 90.0;          // %
    static constexpr double ManyPixels = 20.0;         // % of pixels that differ at all
    static constexpr double FewOfThemClearly = 0.15;   // share of those that differ clearly

    static Reading Interpret(const IlionVisual::Metrics& m, const Conditions& conditions)
    {
        Reading r;

        r.cameraMoved = m.shifted;
        r.sceneChanged = m.sceneMismatch;
        r.generatedFrame = conditions.frameGeneration;
        r.differentGame = conditions.differentGame;

        if (!m.valid)
            return r;

        r.measurable = true;

        if (m.identical)
            r.similarity = Similarity::Identical;
        else if (m.ssim >= SsimVeryClose && m.clearlyPercent < ClearlyVeryClose)
            r.similarity = Similarity::VeryClose;
        else if (m.ssim >= SsimClose)
            r.similarity = Similarity::Close;
        else if (m.ssim >= SsimDifferent)
            r.similarity = Similarity::Different;
        else
            r.similarity = Similarity::VeryDifferent;

        // Where
        double sum = 0.0;

        for (int y = 0; y < IlionVisual::Metrics::GridY; y++)
        {
            for (int x = 0; x < IlionVisual::Metrics::GridX; x++)
                sum += m.regionDifference[y][x];
        }

        const double mean = sum / (IlionVisual::Metrics::GridX * IlionVisual::Metrics::GridY);
        const int worstX = Clamp(m.worstRegionX, 0, IlionVisual::Metrics::GridX - 1);
        const int worstY = Clamp(m.worstRegionY, 0, IlionVisual::Metrics::GridY - 1);

        if (!m.identical && mean >= MeanWorthLocating)
        {
            r.concentration = m.regionDifference[worstY][worstX] / mean;
            r.location = r.concentration >= ConcentratedRatio ? Location::Concentrated : Location::Spread;
        }

        // Edges
        if (m.edgeChangePercent >= EdgeChange)
            r.edges = m.overshootPercent >= OvershootSeen ? Edges::SharperWithOvershoot : Edges::Sharper;
        else if (m.edgeChangePercent <= -EdgeChange)
            r.edges = Edges::Softer;

        r.edgesLost = m.edgesAvailable && m.edgesKeptPercent < EdgesKept;

        r.manySmallDifferences =
            !m.identical && m.differentPercent >= ManyPixels && m.clearlyPercent < m.differentPercent * FewOfThemClearly;

        r.withinSameVersionNoise =
            conditions.sameVersionNoise >= 0.0 && (1.0 - m.ssim) <= conditions.sameVersionNoise * 1.5;

        if (m.Unreliable() || conditions.frameGeneration || conditions.differentGame)
        {
            r.verdict = Verdict::Inconclusive;
        }
        else if (m.identical || r.withinSameVersionNoise ||
                 (r.similarity == Similarity::VeryClose && r.edges == Edges::Similar && !r.edgesLost))
        {
            r.verdict = Verdict::Equivalent;
        }
        else
        {
            r.verdict = Verdict::Detectable;
        }

        r.comparableToNative = conditions.nativeReference && r.verdict != Verdict::Inconclusive;

        return r;
    }

  private:
    static int Clamp(int value, int low, int high) { return value < low ? low : value > high ? high : value; }
};
