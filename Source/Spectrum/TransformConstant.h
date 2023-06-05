/*************************************************************************************

	Signalizer - cross-platform audio visualization plugin - v. 0.x.y

	Copyright (C) 2023 Janus Lynggaard Thorborg (www.jthorborg.com)

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

	See \licenses\ for additional details on licenses associated with this program.

**************************************************************************************

	file:TransformConstant.h

		Static, shared data for a transformation

*************************************************************************************/

#ifndef SIGNALIZER_TRANSFORM_CONSTANT_H
	#define SIGNALIZER_TRANSFORM_CONSTANT_H

	#include "../Signalizer.h"
	#include <cpl/Common.h>
	#include <cpl/gui/CViews.h>
	#include <cpl/Utility.h>
	#include <cpl/dsp/CComplexResonator.h>
	#include <memory>
	#include "SpectrumParameters.h"
	#include <cpl/simd.h>

	namespace Signalizer
	{

		template<typename T>
		class TransformConstant
		{
			using consts = cpl::simd::consts<T>;
		public:

			void setStorage(std::size_t elements, std::size_t effectiveWindowSize, std::size_t& outputTransformSize)
			{
				windowSize = effectiveWindowSize;
				const auto newTransformSize = std::max<std::size_t>(16, cpl::Math::nextPow2Inc(windowSize));
				axisPoints = elements;
				transformSize = outputTransformSize = newTransformSize;
				windowKernel.resize(transformSize);
				mappedFrequencies.resize(axisPoints);
			}

			void checkInvariants()
			{
				CPL_RUNTIME_ASSERTION(mappedFrequencies.size() == axisPoints);
			}

			T mapFrequency(std::size_t axisPoint) const noexcept
			{
				return mappedFrequencies.at(axisPoint);
			}

			void regenerateWindowKernel(/*const*/ cpl::ParameterWindowDesignValue<ParameterSet::ParameterView>& windowDesigner)
			{
				windowKernelScale = windowDesigner.generateWindow<T>(windowKernel, windowSize);
			}

			template<class Y>
			void generateSlopeMap(cpl::uarray<Y> slopeMap, const cpl::PowerSlopeValue::PowerFunction& slopeFunction)
			{
				CPL_RUNTIME_ASSERTION(slopeMap.size() == axisPoints);

				const auto a = static_cast<T>(slopeFunction.a);
				const auto b = static_cast<T>(slopeFunction.b);

				for (std::size_t i = 0; i < axisPoints; ++i)
				{
					slopeMap[i] = static_cast<Y>(b * std::pow<T>(mappedFrequencies[i], a));
				}
			}

			void remapFrequencies(const cpl::Utility::Bounds<double>& viewRect, SpectrumContent::ViewScaling scaling, double minFreq)
			{
				CPL_RUNTIME_ASSERTION(axisPoints > 1);

				double viewSize = viewRect.dist();

				switch (scaling)
				{
				case SpectrumContent::ViewScaling::Linear:
				{
					double halfSampleRate = sampleRate * 0.5;
					double complexFactor = configuration == SpectrumChannels::Complex ? 2.0 : 1.0;
					double freqPerPixel = halfSampleRate / (axisPoints - 1);

					for (std::size_t i = 0; i < axisPoints; ++i)
					{
						mappedFrequencies[i] = static_cast<T>(complexFactor * viewRect.left * halfSampleRate + complexFactor * viewSize * i * freqPerPixel);
					}

					break;
				}
				case SpectrumContent::ViewScaling::Logarithmic:
				{
					double sampleSize = (axisPoints - 1);

					double end = sampleRate / 2;
					if (configuration != SpectrumChannels::Complex)
					{
						for (std::size_t i = 0; i < axisPoints; ++i)
						{
							mappedFrequencies[i] = static_cast<T>(minFreq * std::pow(end / minFreq, viewRect.left + viewSize * (i / sampleSize)));
						}

					}
					else
					{
						for (std::size_t i = 0; i < axisPoints; ++i)
						{
							auto arg = viewRect.left + viewSize * i / sampleSize;
							if (arg < 0.5)
							{
								mappedFrequencies[i] = static_cast<T>(minFreq * std::pow(end / minFreq, arg * 2));
							}
							else
							{
								arg -= 0.5;
								auto power = minFreq * std::pow(end / minFreq, 1.0 - arg * 2);
								mappedFrequencies[i] = static_cast<T>(end + (end - power));
							}
						}

					}
					break;
				}
				}
			}


			std::size_t getStateConfigurationChannels() const noexcept
			{
				return configuration > SpectrumChannels::OffsetForMono ? 2 : 1;
			}

			/// <summary>
			/// How the incoming data is interpreted, channel-wise.
			/// </summary>
			SpectrumChannels configuration;
			/// <summary>
			/// The window function applied to the input. Precomputed into windowKernel.
			/// </summary>
			cpl::dsp::WindowTypes dspWindow;
			T sampleRate;

			std::size_t axisPoints{}, transformSize{}, windowSize{};

			/// <summary>
			/// An array, of axisPoints size, with each element being the frequency for the filter of
			/// the corresponding logical display pixel unit.
			/// </summary>
			std::vector<T> mappedFrequencies;

			/// <summary>
			/// The time-domain representation of the dsp-window applied to fourier transforms.
			/// </summary>
			cpl::aligned_vector<T, 32> windowKernel;
			T windowKernelScale;
		};
	}



#endif
