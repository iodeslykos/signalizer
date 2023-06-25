/*************************************************************************************

	Signalizer - cross-platform audio visualization plugin - v. 0.x.y

	Copyright (C) 2016 Janus Lynggaard Thorborg (www.jthorborg.com)

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

	file:SpectrumDSP.cpp

		Implementation of the dsp code for the spectrum.

*************************************************************************************/

#include "Spectrum.h"
#include <cpl/ffts.h>
#include <cpl/system/SysStats.h>
#include <cpl/lib/LockFreeDataQueue.h>
#include <cpl/stdext.h>
#include <cpl/JobSystem.h>
#include <execution>

namespace Signalizer
{

	std::size_t Spectrum::getBlobSamples() const noexcept
	{
		return std::max<std::size_t>(10, static_cast<std::size_t>(content->blobSize.getTransformedValue() * 0.001 * getSampleRate()));
	}

	void Spectrum::resetState()
	{
		flags.resetStateBuffers = true;
	}

	bool Spectrum::StreamState::prepareTransform(const Constant& constant, const AudioStream::AudioBufferAccess & audio)
	{
		if (audio.getNumChannels() < 2)
			return false;

		{
			Stream::AudioBufferView views[2] = { audio.getView(0), audio.getView(1) };

			// we need the buffers to be same size, and at least equal or greater in size of ours (cant fill in information).
			// this is a very rare condition that can be solved by locking the audio access during the flags update and this
			// call, however to avoid unnecessary locks we skip a frame instead once in a while.
			if (views[0].size() != views[1].size() || views[0].size() < constant.windowSize)
				return false;

			// can't underflow
			std::size_t offset = views[0].size() - constant.windowSize;

			switch (constant.algo)
			{
			case SpectrumContent::TransformAlgorithm::FFT:
			{
				auto buffer = getAudioMemory<std::complex<fftType>>();
				std::size_t channel = 1;
				std::size_t i = 0;

				switch (constant.configuration)
				{
				case SpectrumChannels::Left:
					channel = 0;
				case SpectrumChannels::Right:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[channel].getItRange(indice);
						auto it = views[channel].getItIndex(indice);

						if (range > offset)
						{
							range -= offset;
							it += offset;

							while (range--)
							{
								buffer[i] = *it++ * constant.windowKernel[i];
								i++;
							}

							offset = 0;
						}
						else
						{
							offset -= range;
						}

					}
					break;
				}
				case SpectrumChannels::Merge:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[0].getItRange(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);

						if (range > offset)
						{
							range -= offset;
							left += offset;
							right += offset;

							while (range--)
							{
								buffer[i] =	(*left++ + *right++) * constant.windowKernel[i] * 0.5f;
								i++;
							}
							offset = 0;
						}
						else
						{
							offset -= range;
						}
					}
					break;
				}
				case SpectrumChannels::Side:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[0].getItRange(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);

						if (range > offset)
						{
							range -= offset;
							left += offset;
							right += offset;

							while (range--)
							{
								buffer[i] = (*left++ - *right++) * constant.windowKernel[i] * 0.5f;
								i++;
							}

							offset = 0;
						}
						else
						{
							offset -= range;
						}
					}
					break;
				}
				case SpectrumChannels::MidSide:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[0].getItRange(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);

						if (range > offset)
						{
							range -= offset;
							left += offset;
							right += offset;

							while (range--)
							{
								buffer[i] = std::complex<fftType>
								(
									(*left + *right) * constant.windowKernel[i] * 0.5f,
									(*left - *right) * constant.windowKernel[i] * 0.5f
								);
								left++;
								right++;
								i++;
							}

							offset = 0;
						}
						else
						{
							offset -= range;
						}
					}
					break;
				}
				case SpectrumChannels::Phase:
				case SpectrumChannels::Separate:
				case SpectrumChannels::Complex:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[0].getItRange(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);

						if (range > offset)
						{
							range -= offset;
							left += offset;
							right += offset;

							while (range--)
							{
								buffer[i] = std::complex<fftType>
								{
									*left++ * constant.windowKernel[i],
									*right++ * constant.windowKernel[i]
								};
								i++;
							}
							offset = 0;
						}
						else
						{
							offset -= range;
						}
					}
					break;
				}
				}
				//zero-pad until buffer is filled

				for (size_t pad = i; pad < constant.transformSize; ++pad)
				{
					buffer[pad] = (fftType)0;
				}

				break;
			}
			break;
			}
		}
		return true;
	}

	bool Spectrum::StreamState::prepareTransform(const Constant& constant, const AudioStream::AudioBufferAccess & audio, Spectrum::fpoint ** preliminaryAudio, std::size_t numChannels, std::size_t numSamples)
	{
		{
			Stream::AudioBufferView views[2] = { audio.getView(0), audio.getView(1) };

			// we need the buffers to be same size, and at least equal or greater in size of ours (cant fill in information).
			// this is a very rare condition that can be solved by locking the audio access during the flags update and this
			// call, however to avoid unnecessary locks we skip a frame instead once in a while.
			if (views[0].size() != views[1].size() || views[0].size() < constant.windowSize)
				return false;

			// extra discarded samples in case the incoming buffer is larger than our own
			std::size_t extraDiscardedSamples = views[0].size() - constant.windowSize;

			switch (constant.algo)
			{
			case SpectrumContent::TransformAlgorithm::FFT:
			{
				auto buffer = getAudioMemory<std::complex<fftType>>();
				std::size_t channel = 1;
				std::size_t i = 0;
				std::size_t stop = std::min(numSamples, constant.windowSize);

				std::size_t offset = stop + extraDiscardedSamples;
				auto sizeToStopAt = constant.windowSize - offset;

				switch (constant.configuration)
				{
				case SpectrumChannels::Left:
					channel = 0;
				case SpectrumChannels::Right:
				{
					// get start from buffers - first indice is a special case.
					// notice the buffers are reversed in time, so we pull old data first with an offset,
					// and then fill in the new
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[channel].getItRange(indice);
						auto it = views[channel].getItIndex(indice);

						if (range > offset)
						{

							// advance our current progress (from preliminary audio)
							// notice this only has effect for the first buffer coming in here.
							range -= offset;
							it += offset;

							while (range-- && i < sizeToStopAt)
							{
								buffer[i] = *it++ * constant.windowKernel[i];
								i++;
							}

							offset = 0;

						}
						else
						{
							offset -= range;
						}
					}


					// process preliminary
					for (std::size_t k = 0; k < stop; ++i, k++)
					{
						buffer[i] = preliminaryAudio[channel][k] * constant.windowKernel[i];
					}


					break;
				}
				case SpectrumChannels::Merge:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[channel].getItRange(indice);
						auto it = views[channel].getItIndex(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);
						if (range > offset)
						{

							range -= offset;
							it += offset;

							while (range-- && i < sizeToStopAt)
							{
								buffer[i] = (*left++ + *right++) * constant.windowKernel[i] * 0.5f;
								i++;
							}

							offset = 0;

						}
						else
						{
							offset -= range;
						}
					}

					for (std::size_t k = 0; k < stop; ++i, k++)
					{
						buffer[i] = (preliminaryAudio[0][k] + preliminaryAudio[1][k]) * constant.windowKernel[i] * (fftType)0.5;
					}

					break;
				}
				case SpectrumChannels::Side:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[channel].getItRange(indice);
						auto it = views[channel].getItIndex(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);
						if (range > offset)
						{

							range -= offset;
							it += offset;

							while (range-- && i < sizeToStopAt)
							{
								buffer[i] = (*left++ - *right++) * constant.windowKernel[i] * (fftType)0.5;
								i++;
							}

							offset = 0;

						}
						else
						{
							offset -= range;
						}
					}

					for (std::size_t k = 0; k < stop; ++i, k++)
					{
						buffer[i] = (preliminaryAudio[0][k] - preliminaryAudio[1][k]) * constant.windowKernel[i] * (fftType)0.5;
					}

					break;
				}
				case SpectrumChannels::MidSide:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[channel].getItRange(indice);
						auto it = views[channel].getItIndex(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);
						if (range > offset)
						{

							range -= offset;
							it += offset;

							while (range-- && i < sizeToStopAt)
							{
								buffer[i] = std::complex<fftType>
								(
									(*left + *right) * constant.windowKernel[i] * (fftType)0.5,
									(*left - *right) * constant.windowKernel[i] * (fftType)0.5
								);
								left++;
								right++;
								i++;
							}

							offset = 0;

						}
						else
						{
							offset -= range;
						}
					}

					for (std::size_t k = 0; k < stop; ++i, k++)
					{
						buffer[i] = std::complex<fftType>
						(
							(preliminaryAudio[0][k] + preliminaryAudio[1][k]) * constant.windowKernel[i] * (fftType)0.5,
							(preliminaryAudio[0][k] - preliminaryAudio[1][k]) * constant.windowKernel[i] * (fftType)0.5
						);
					}

					break;
				}
				case SpectrumChannels::Phase:
				case SpectrumChannels::Separate:
				case SpectrumChannels::Complex:
				{

					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[channel].getItRange(indice);
						auto it = views[channel].getItIndex(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);
						if (range > offset)
						{

							range -= offset;
							it += offset;

							while (range-- && i < sizeToStopAt)
							{
								buffer[i] = std::complex<fftType>
								{
									*left++ * constant.windowKernel[i],
									*right++ * constant.windowKernel[i]
								};
								i++;
							}

							offset = 0;

						}
						else
						{
							offset -= range;
						}
					}

					for (std::size_t k = 0; k < stop; ++i, k++)
					{
						buffer[i] = std::complex<fftType>
						(
							preliminaryAudio[0][k] * constant.windowKernel[i],
							preliminaryAudio[1][k] * constant.windowKernel[i]
						);
					}

					break;
				}
				}
				//zero-pad until buffer is filled
				for (size_t pad = i; pad < constant.transformSize; ++pad)
				{
					buffer[pad] = 0;
				}

				break;
			}
			break;
			}
		}
		return true;
	}

	void Spectrum::StreamState::doTransform(const Constant& constant)
	{
		switch (constant.algo)
		{
			case SpectrumContent::TransformAlgorithm::FFT:
			{
				if(constant.transformSize > 0)
					signaldust::DustFFT_fwdDa(getAudioMemory<double>(), static_cast<unsigned int>(constant.transformSize));

				break;
			}
		}
	}

	template<class V2>
		void Spectrum::mapAndTransformDFTFilters(SpectrumChannels type, const V2 & newVals, std::size_t size, double lowDbs, double highDbs, float clip)
		{
			double lowerFraction = cpl::Math::dbToFraction<double>(lowDbs);
			double upperFraction = cpl::Math::dbToFraction<double>(highDbs);

			auto deltaYRecip = static_cast<fpoint>(1.0 / log(upperFraction / lowerFraction));
			auto minFracRecip = static_cast<fpoint>(1.0 / lowerFraction);
			auto halfRecip = fpoint(0.5);

			fpoint lowerClip = (fpoint)clip;

			switch (type)
			{
			case SpectrumChannels::Left:
			case SpectrumChannels::Merge:
			case SpectrumChannels::Right:
			case SpectrumChannels::Side:
			case SpectrumChannels::Complex:
			{

				for (cpl::Types::fint_t i = 0; i < size; ++i)
				{

					auto newReal = newVals[i * 2];
					auto newImag = newVals[i * 2 + 1];
					// mag = abs(cmplx)
					auto magnitude = sqrt(newReal * newReal + newImag * newImag);


					for (std::size_t k = 0; k < lineGraphs.size(); ++k)
					{
						lineGraphs[k].states[i].magnitude *= lineGraphs[k].filter.pole;

						if (magnitude > lineGraphs[k].states[i].magnitude)
						{
							lineGraphs[k].states[i].magnitude = (fpoint)magnitude;
						}

						auto deltaX = slopeMap[i] * lineGraphs[k].states[i].magnitude * minFracRecip;
						// deltaX mostly zero here - add simd check
						auto result = deltaX > 0 ? std::log(deltaX) * deltaYRecip : lowerClip;
						lineGraphs[k].results[i].magnitude = (fpoint)result;
						lineGraphs[k].results[i].phase = 0;
					}

				}
				break;
			}
			case SpectrumChannels::Separate:
			case SpectrumChannels::MidSide:
			{

				for (cpl::Types::fint_t i = 0; i < size; ++i)
				{

					auto lreal = newVals[i * 2];
					auto rreal = newVals[i * 2 + size * 2];
					auto limag = newVals[i * 2 + 1];
					auto rimag = newVals[i * 2 + size * 2 + 1];
					// mag = abs(cmplx)
					auto lmag = sqrt(lreal * lreal + limag * limag);
					auto rmag = sqrt(rreal * rreal + rimag * rimag);

					for (std::size_t k = 0; k < lineGraphs.size(); ++k)
					{
						lineGraphs[k].states[i].leftMagnitude *= lineGraphs[k].filter.pole;
						lineGraphs[k].states[i].rightMagnitude *= lineGraphs[k].filter.pole;

						if (lmag > lineGraphs[k].states[i].leftMagnitude)
						{
							lineGraphs[k].states[i].leftMagnitude = (fpoint)lmag;
						}
						if (rmag > lineGraphs[k].states[i].rightMagnitude)
						{
							lineGraphs[k].states[i].rightMagnitude = (fpoint)rmag;
						}
						// log10(y / _min) / log10(_max / _min);
						auto deltaLX = slopeMap[i] * lineGraphs[k].states[i].leftMagnitude * minFracRecip;
						auto deltaRX = slopeMap[i] * lineGraphs[k].states[i].rightMagnitude * minFracRecip;
						// deltaX mostly zero here - add simd check
						auto lResult = deltaLX > 0 ? std::log(deltaLX) * deltaYRecip : lowerClip;
						auto rResult = deltaRX > 0 ? std::log(deltaRX) * deltaYRecip : lowerClip;
						lineGraphs[k].results[i].leftMagnitude = (fpoint)lResult;
						lineGraphs[k].results[i].rightMagnitude = (fpoint)rResult;
					}
				}
				break;
			}
			case SpectrumChannels::Phase:
			{
				fpoint phaseFilters[SpectrumContent::LineGraphs::LineEnd];

				for (std::size_t k = 0; k < lineGraphs.size(); ++k)
					phaseFilters[k] = std::pow(lineGraphs[k].filter.pole, 0.3);

				for (cpl::Types::fint_t i = 0; i < size; ++i)
				{

					auto mag = newVals[i * 2];
					auto phase = newVals[i * 2 + 1];
					// mag = abs(cmplx)

					mag *= halfRecip;

					for (std::size_t k = 0; k < lineGraphs.size(); ++k)
					{
						lineGraphs[k].states[i].magnitude *= lineGraphs[k].filter.pole;

						if (mag > lineGraphs[k].states[i].magnitude)
						{
							lineGraphs[k].states[i].magnitude = (fpoint)mag;
						}
						phase *= mag;

						lineGraphs[k].states[i].phase = phase + phaseFilters[k] * (lineGraphs[k].states[i].phase - phase);


						// log10(y / _min) / log10(_max / _min);
						// deltaX mostly zero here - add simd check
						auto deltaX = slopeMap[i] * lineGraphs[k].states[i].magnitude * minFracRecip;
						auto deltaY = slopeMap[i] * lineGraphs[k].states[i].phase * minFracRecip;
						// deltaX mostly zero here - add simd check
						auto result = deltaX > 0 ? std::log(deltaX) * deltaYRecip : lowerClip;
						lineGraphs[k].results[i].magnitude = (fpoint)result;
						lineGraphs[k].results[i].phase = (fpoint)(deltaY > 0 ? std::log(deltaY) * deltaYRecip : lowerClip);
					}
				}
				break;
			}
			};


		}

	template<class InVector>
		void Spectrum::postProcessTransform(const InVector & transform)
		{
			mapAndTransformDFTFilters(state.configuration, transform, state.axisPoints, content->lowDbs.getTransformedValue(), content->highDbs.getTransformedValue(), content->lowDbs.getTransformer().transform(0));
		}

	void Spectrum::postProcessStdTransform(const Constant& constant, const StreamState& state)
	{
		if (constant.algo == SpectrumContent::TransformAlgorithm::FFT)
			postProcessTransform(state.getTransformResult<fftType>());
		else
			postProcessTransform(state.getTransformResult<fpoint>());
	}

	void Spectrum::StreamState::mapToLinearSpace(const Constant& constant)
	{
		using namespace cpl;

		switch (constant.algo)
		{
		case SpectrumContent::TransformAlgorithm::FFT:
		{
			const auto lanczosFilterSize = 5;
			cpl::ssize_t bin = 0, oldBin = 0, maxLBin, maxRBin = 0;
			Types::fsint_t N = static_cast<Types::fsint_t>(constant.transformSize);

			// we rely on mapping indexes, so we need N > 2 at least.
			if (N < 3 || constant.sampleRate < 1)
				return;

			std::size_t numBins = N >> 1;
			auto const topFrequency = constant.sampleRate / 2;
			auto const freqToBin = static_cast<fftType>(numBins / topFrequency);

			typedef fftType ftype;

			std::complex<ftype> leftMax, rightMax;

			ftype maxLMag, maxRMag, newLMag, newRMag;

			// complex transform results, N + 1 size
			auto csf = getAudioMemory<std::complex<ftype>>();
			// buffer for single results, numPoints * 2 size
			ftype * wsp = getWorkingMemory<ftype>();
			// buffer for complex results, numPoints size
			std::complex<ftype> * csp = getWorkingMemory<std::complex<ftype>>();

			// this will make scaling correct regardless of amount of zero-padding
			// notice the 0.5: fft's of size 32 will output 16 for exact frequency bin matches,
			// so we halve the reciprocal scaling factor to normalize the size.
			auto const invSize = static_cast<fftType>(constant.windowKernelScale / (constant.windowSize * 0.5));

			switch (constant.configuration)
			{
			case SpectrumChannels::Left:
			case SpectrumChannels::Right:
			case SpectrumChannels::Merge:
			case SpectrumChannels::Side:
			{
				oldBin = constant.mappedFrequencies[0] * freqToBin;

				// the DC (0) and nyquist bin are NOT 'halved' due to the symmetric nature of the fft,
				// so halve these:
				csf[0] *= 0.5;
				csf[N >> 1] *= 0.5;

				// TODO: Vectorize
				for (std::size_t i = 0; i < numBins; ++i)
				{
					csf[i] = std::abs(csf[i]);
				}

				double fftBandwidth = 1.0 / numBins;
				//double pxlBandwidth = 1.0 / numPoints;
				cpl::Types::fint_t x = 0;
				switch (constant.binPolation)
				{
				case SpectrumContent::BinInterpolation::None:
					for (x = 0; x < constant.axisPoints - 1; ++x)
					{
						// bandwidth of the filter for this 'line', or point
						double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
						// as long as the bandwidth is smaller than our fft resolution, we interpolate the points
						// otherwise, break out and sample the max values of the bins inside the bandwidth
						if (bwForLine > fftBandwidth)
							break;
						// +0.5 to centerly space bins.
						auto index = Math::confineTo((std::size_t)(constant.mappedFrequencies[x] * freqToBin + 0.5), 0, numBins);
						csp[x] = invSize * csf[index];
					}
					break;
				case SpectrumContent::BinInterpolation::Linear:
					for (x = 0; x < constant.axisPoints - 1; ++x)
					{
						double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
							break;

						csp[x] = invSize * dsp::linearFilter<std::complex<ftype>>(csf, N, constant.mappedFrequencies[x] * freqToBin);
					}
					break;
				case SpectrumContent::BinInterpolation::Lanczos:
					for (x = 0; x < constant.axisPoints - 1; ++x)
					{

						double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
							break;

						csp[x] = invSize * dsp::lanczosFilter<std::complex<ftype>, true>(csf, N, constant.mappedFrequencies[x] * freqToBin, lanczosFilterSize);
					}
					break;
				default:
					break;
				}

				oldBin = constant.mappedFrequencies[x] * freqToBin;

				for (; x < constant.axisPoints; ++x)
				{
					maxLMag = maxRMag = newLMag = newRMag = 0;

					bin = static_cast<std::size_t>(constant.mappedFrequencies[x] * freqToBin);
#ifdef DEBUG
					if ((std::size_t)bin >= constant.transformSize)
						CPL_RUNTIME_EXCEPTION("Corrupt frequency mapping!");
#endif
					maxRBin = maxLBin = bin;

					auto diff = bin - oldBin;
					auto counter = diff ? 1 : 0;
					// here we loop over all the bins that is mapped for a single coordinate
					do
					{
						auto offset = oldBin + counter;
						newLMag = Math::square(csf[offset]);
						// select highest number in this chunk for display. Not exactly correct, though.
						if (newLMag > maxLMag)
						{
							maxLBin = oldBin + counter;
							maxLMag = newLMag;
						}
						counter++;
						diff--;
					} while (diff > 0);

					csp[x] = invSize * csf[maxLBin];

					oldBin = bin;
				}

				break;
			}
			case SpectrumChannels::Phase:
			{
				// two-for-one pass, first channel is 0... N/2 -1, second is N/2 .. N -1
				dsp::separateTransformsIPL(csf, N);

				// fix up DC and nyquist bins (see previous function documentation)
				csf[N] = csf[0].imag() * 0.5;
				csf[0] = csf[0].real() * 0.5;
				csf[N >> 1] *= 0.5;
				csf[(N >> 1) - 1] *= 0.5;

				// TODO: rewrite this.
				// firstly, we have to do a phase cancellation pass.
				// this is because magnitude interpolation is wrong for
				// complex vectors, it needs to be done on magnitude.
				// however, phase calculation needs to be done on vectors.
				// so we first do a phase cancellation pass, and afterwards
				// absolute the lower bottom of the transform, that needs to be interpolated.

				// The index of the transform, where the bandwidth is higher than mapped pixels (so no more interpolation is needed)
				// TODO: This can be calculated from view mapping scale and N pixels.
				std::size_t bandWidthBreakingPoint = constant.axisPoints;

				double fftBandwidth = 1.0 / numBins;
				//double pxlBandwidth = 1.0 / numPoints;
				cpl::Types::fint_t x = 0;
				switch (constant.binPolation)
				{
				case SpectrumContent::BinInterpolation::Linear:
				{
					// phase pass
					for (x = 0; x < constant.axisPoints - 1; ++x)
					{
						double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
						{
							bandWidthBreakingPoint = x;
							break;
						}

						auto iLeft = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, constant.mappedFrequencies[x] * freqToBin);
						auto iRight = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, N - (constant.mappedFrequencies[x] * freqToBin));

						auto cancellation = invSize * std::sqrt(Math::square(iLeft + iRight));
						auto mid = invSize * (std::abs(iLeft) + std::abs(iRight));

						wsp[x * 2 + 1] = ftype(1) - (mid > 0 ? (cancellation / mid) : 0);
					}

					// normalize vectors we need to magnitude-interpolate.
					/* auto stop = std::size_t(bandWidthBreakingPoint * pointsToBin);
					for (std::size_t i = 1; i < stop; ++i)
					{
						csf[i] = std::abs(csf[i]);
						csf[N - i] = std::abs(csf[N - i]);
					} */

					std::size_t normalizedPosition = 0U;

					// magnitude pass.
					// TODO: we aren't calculating the last point, because that means we have to normalize one position further
					// to ensure correct magnitude interpretation. That will however nullify the phase response in the next pass,
					// since normalization eliminates phase information.

					const std::size_t linearFilterSize = 1;

					for (x = 0; x < bandWidthBreakingPoint; ++x)
					{
						// fractional bin position
						auto binPosition = constant.mappedFrequencies[x] * freqToBin;

						// normalize vectors we need to magnitude-interpolate.
						while ((binPosition + linearFilterSize) > normalizedPosition && x < bandWidthBreakingPoint - (linearFilterSize + 1))
						{
							csf[normalizedPosition] = std::abs(csf[normalizedPosition]);
							csf[N - normalizedPosition] = std::abs(csf[N - normalizedPosition]);
							normalizedPosition++;
						}

						auto iLeft = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, binPosition);
						auto iRight = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, N - binPosition);
						// TODO: abs not really needed, because of normalization.
						wsp[x * 2] = invSize * (std::abs(iLeft) + std::abs(iRight));

					}
					break;
				}
				case SpectrumContent::BinInterpolation::Lanczos:
				{
					for (x = 0; x < constant.axisPoints - 1; ++x)
					{

						double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
						{
							bandWidthBreakingPoint = x;
							break;
						}

						auto iLeft = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, constant.mappedFrequencies[x] * freqToBin, lanczosFilterSize);
						auto iRight = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, N - (constant.mappedFrequencies[x] * freqToBin), lanczosFilterSize);

						auto cancellation = invSize * std::sqrt(Math::square(iLeft + iRight));
						auto mid = invSize * (std::abs(iLeft) + std::abs(iRight));

						wsp[x * 2 + 1] = ftype(1) - (mid > 0 ? (cancellation / mid) : 0);
					}

					std::size_t normalizedPosition = 0U;

					// magnitude pass.
					// TODO: we aren't calculating the last point, because that means we have to normalize one position further
					// to ensure correct magnitude interpretation. That will however nullify the phase response in the next pass,
					// since normalization eliminates phase information.


					for (x = 0; x < bandWidthBreakingPoint; ++x)
					{
						// fractional bin position
						auto binPosition = constant.mappedFrequencies[x] * freqToBin;

						// normalize vectors we need to magnitude-interpolate.
						while ((binPosition + lanczosFilterSize) > normalizedPosition && x < bandWidthBreakingPoint - (lanczosFilterSize + 1))
						{
							csf[normalizedPosition] = std::abs(csf[normalizedPosition]);
							csf[N - normalizedPosition] = std::abs(csf[N - normalizedPosition]);
							normalizedPosition++;
						}

						auto iLeft = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, constant.mappedFrequencies[x] * freqToBin, lanczosFilterSize);
						auto iRight = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, N - (constant.mappedFrequencies[x] * freqToBin), lanczosFilterSize);

						wsp[x * 2] = invSize * (std::abs(iLeft) + std::abs(iRight));
					}

					break;
				}
				default:
					for (x = 0; x < constant.axisPoints - 1; ++x)
					{
						// bandwidth of the filter for this 'line', or point
						double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
						// as long as the bandwidth is smaller than our fft resolution, we interpolate the points
						// otherwise, break out and sample the max values of the bins inside the bandwidth
						if (bwForLine > fftBandwidth)
							break;

						// +0.5 to centerly space bins.
						auto index = Math::confineTo((std::size_t)(constant.mappedFrequencies[x] * freqToBin + 0.5), 0, numBins - 1);
						auto iLeft = csf[index];
						auto iRight = csf[N - index];

						auto cancellation = invSize * std::abs(iLeft + iRight);
						auto mid = invSize * (std::abs(iLeft) + std::abs(iRight));

						wsp[x * 2] = mid;

						wsp[x * 2 + 1] = ftype(1) - (mid > 0 ? (cancellation / mid) : 0);
					}
					break;
				}


				// the process after interpolation is much simpler, as we dont have to account
				// for wrongly interpolation of phase-mangled vectors.
				if(x < constant.axisPoints)
					oldBin = constant.mappedFrequencies[x] * freqToBin;



				for (; x < constant.axisPoints; ++x)
				{
					std::size_t maxBin = 0;
					ftype maxValue = 0, newMag = 0;
					bin = static_cast<std::size_t>(constant.mappedFrequencies[x] * freqToBin);
#ifdef DEBUG
					if ((std::size_t)bin >= constant.transformSize)
						CPL_RUNTIME_EXCEPTION("Corrupt frequency mapping!");
#endif

					auto diff = bin - oldBin;
					auto counter = diff ? 1 : 0;
					// here we loop over all the bins that is mapped for a single coordinate
					do
					{
						auto offset = oldBin + counter;
						newMag = std::max(Math::square(csf[offset]), Math::square(csf[N - offset]));
						// select highest number in this chunk for display. Not exactly correct, though.
						if (newMag > maxValue)
						{
							maxValue = newMag;
							maxBin = oldBin + counter;
						}
						counter++;
						diff--;
					} while (diff > 0);

					leftMax = csf[maxBin];
					rightMax = csf[N - maxBin];

					auto interference = invSize * std::abs(leftMax + rightMax);
					auto mid = invSize * (std::abs(leftMax) + std::abs(rightMax));
					auto cancellation = interference / mid;
					wsp[x * 2] = mid;
					wsp[x * 2 + 1] = ftype(1) - (mid > 0 ? cancellation : 0);

					oldBin = bin;
				}

				break;
			}
			case SpectrumChannels::Separate:
			case SpectrumChannels::MidSide:
			{
				// two-for-one pass, first channel is 0... N/2 -1, second is N/2 .. N -1
				dsp::separateTransformsIPL(csf, N);

				// fix up DC and nyquist bins (see previous function documentation)
				csf[N] = csf[0].imag() * 0.5;
				csf[0] = csf[0].real() * 0.5;
				csf[N >> 1] *= 0.5;
				csf[(N >> 1) - 1] *= 0.5;

				for (decltype(N) i = 1; i < N; ++i)
				{
					csf[i] = std::abs(csf[i]);
				}

				// The index of the transform, where the bandwidth is higher than mapped pixels (so no more interpolation is needed)
				// TODO: This can be calculated from view mapping scale and N pixels.
				std::size_t bandWidthBreakingPoint = constant.axisPoints - 1;

				double fftBandwidth = 1.0 / numBins;
				//double pxlBandwidth = 1.0 / numPoints;
				cpl::Types::fint_t x;
				switch (constant.binPolation)
				{
				case SpectrumContent::BinInterpolation::Linear:

					// phase pass
					for (x = 0; x < constant.axisPoints - 1; ++x)
					{
						double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
						{
							bandWidthBreakingPoint = x;
							break;
						}

						auto iLeft = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, constant.mappedFrequencies[x] * freqToBin);
						auto iRight = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, N - (constant.mappedFrequencies[x] * freqToBin));

						csp[x] = invSize * iLeft;
						csp[constant.axisPoints + x] = invSize * iRight;
					}

					break;
				case SpectrumContent::BinInterpolation::Lanczos:
					for (x = 0; x < constant.axisPoints - 1; ++x)
					{

						double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
						{
							bandWidthBreakingPoint = x;
							break;
						}

						auto iLeft = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, constant.mappedFrequencies[x] * freqToBin, 5);
						auto iRight = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, N - (constant.mappedFrequencies[x] * freqToBin), lanczosFilterSize);

						csp[x] = invSize * iLeft;
						csp[constant.axisPoints + x] = invSize * iRight;
					}

					break;
				default:
					for (x = 0; x < constant.axisPoints - 1; ++x)
					{
						// bandwidth of the filter for this 'line', or point
						double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
						// as long as the bandwidth is smaller than our fft resolution, we interpolate the points
						// otherwise, break out and sample the max values of the bins inside the bandwidth
						if (bwForLine > fftBandwidth)
							break;

						// +0.5 to centerly space bins.
						auto index = Math::confineTo((std::size_t)(constant.mappedFrequencies[x] * freqToBin + 0.5), 0, numBins);

						csp[x] = invSize * csf[index];
						csp[constant.axisPoints + x] = invSize * csf[N - index];
					}
					break;
				}


				// the process after interpolation is much simpler, as we dont have to account
				// for wrongly interpolation of phase-mangled vectors.

				oldBin = constant.mappedFrequencies[x] * freqToBin;

				for (; x < constant.axisPoints; ++x)
				{
					maxLMag = maxRMag = newLMag = newRMag = 0;

					bin = static_cast<std::size_t>(constant.mappedFrequencies[x] * freqToBin);
#ifdef DEBUG
					if ((std::size_t)bin >= constant.transformSize)
						CPL_RUNTIME_EXCEPTION("Corrupt frequency mapping!");
#endif
					maxRBin = maxLBin = bin;

					auto diff = bin - oldBin;
					auto counter = diff ? 1 : 0;
					// here we loop over all the bins that is mapped for a single coordinate
					do
					{
						auto offset = oldBin + counter;
						//offset <<= 1;
						newLMag = Math::square(csf[offset]);
						newRMag = Math::square(csf[N - offset]);
						// select highest number in this chunk for display. Not exactly correct, though.
						if (newLMag > maxLMag)
						{
							maxLBin = oldBin + counter;
							maxLMag = newLMag;
						}

						if (newRMag > maxRMag)
						{
							maxRBin = N - (oldBin + counter);
							maxRMag = newRMag;
						}

						counter++;
						diff--;
					} while (diff > 0);

					csp[x] = invSize * csf[maxLBin];
					csp[constant.axisPoints + x] = invSize * csf[maxRBin];
					oldBin = bin;
				}
			}
			break;
			case SpectrumChannels::Complex:
			{
				// two-for-one pass, first channel is 0... N/2 -1, second is N/2 .. N -1

				// fix up DC and nyquist bins (see previous function documentation)
				//csf[N] = csf[0].imag() * 0.5;
				csf[0] *= (fftType) 0.5;

				double fftBandwidth = 1.0 / (numBins * 2);
				//double pxlBandwidth = 1.0 / numPoints;
				cpl::Types::fint_t x = 0;

				for (decltype(N) i = 1; i < N; ++i)
				{
					csf[i] = std::abs(csf[i]);
				}

				while(x < constant.axisPoints)
				{

					switch (constant.binPolation)
					{
					case SpectrumContent::BinInterpolation::Linear:
					{
						for (; x < constant.axisPoints; ++x)
						{
							if (x != constant.axisPoints - 1)
							{
								double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
								if (bwForLine > fftBandwidth)
									break;
							}
							csp[x] = invSize * dsp::linearFilter<std::complex<ftype>>(csf, N + 1, constant.mappedFrequencies[x] * freqToBin);
						}
						break;
					}
					case SpectrumContent::BinInterpolation::Lanczos:
					{
						for (; x < constant.axisPoints; ++x)
						{
							if (x != constant.axisPoints - 1)
							{
								double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
								if (bwForLine > fftBandwidth)
									break;
							}

							csp[x] = invSize * dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, constant.mappedFrequencies[x] * freqToBin, lanczosFilterSize);
						}

						break;
					}
					default:
						for (; x < constant.axisPoints; ++x)
						{
							if (x != constant.axisPoints - 1)
							{
								double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
								if (bwForLine > fftBandwidth)
									break;
							}
							// +0.5 to centerly space bins.
							auto index = Math::confineTo((std::size_t)(constant.mappedFrequencies[x] * freqToBin + 0.5), 0, N);
							csp[x] = invSize * csf[index];
						}
						break;
					}
					if(x != constant.axisPoints)
						oldBin = constant.mappedFrequencies[x] * freqToBin;

					for (; x < constant.axisPoints; ++x)
					{
						maxLMag = maxRMag = newLMag = newRMag = 0;

						bin = static_cast<std::size_t>(constant.mappedFrequencies[x] * freqToBin);
#ifdef DEBUG
						if ((std::size_t)bin >= constant.transformSize)
							CPL_RUNTIME_EXCEPTION("Corrupt frequency mapping!");
#endif
						maxRBin = maxLBin = bin;
						if (x != constant.axisPoints - 1)
						{
							double bwForLine = (constant.mappedFrequencies[x + 1] - constant.mappedFrequencies[x]) / topFrequency;
							if (bwForLine < fftBandwidth)
								break;
						}

						auto diff = bin - oldBin;
						auto counter = diff ? 1 : 0;
						// here we loop over all the bins that is mapped for a single coordinate
						do
						{
							auto offset = oldBin + counter;
							//offset <<= 1;
							newLMag = Math::square(csf[offset]);
							// select highest number in this chunk for display. Not exactly correct, though.
							if (newLMag > maxLMag)
							{
								maxLBin = oldBin + counter;
								maxLMag = newLMag;
							}

							counter++;
							diff--;
						} while (diff > 0);

						csp[x] = invSize * csf[maxLBin];
						oldBin = bin;
					}
				}
			}

			break;
			}
			break;
		}
		case SpectrumContent::TransformAlgorithm::RSNT:
		{

			std::complex<float> * wsp = getWorkingMemory<std::complex<float>>();
			auto configurationChannels = constant.getStateConfigurationChannels();
			// TODO: recursive lock?
			std::size_t filtersPerChannel = copyResonatorStateInto<fpoint>(constant, constant.dspWindow, wsp, configurationChannels) / configurationChannels;

			switch (constant.configuration)
			{
			case SpectrumChannels::Phase:
			{
				for (std::size_t x = 0; x < filtersPerChannel; ++x)
				{

					auto iLeft = wsp[x];
					auto iRight = wsp[x + filtersPerChannel];

					auto cancellation = std::sqrt(Math::square(iLeft + iRight));
					auto mid = std::abs(iLeft) + std::abs(iRight);


					wsp[x] = std::complex<float>(mid, fpoint(1) - (mid > 0 ? (cancellation / mid) : 0));

				}

				break;
			}
			// rest of cases does not need any handling
			}

		}
		break;
		}
	}

	void Spectrum::StreamState::remapResonator(Constant& constant)
	{
		cresonator.match(constant.Resonator);
	}


	bool Spectrum::processNextSpectrumFrame()
	{
		SFrameQueue::ElementAccess access;
		if (processor->frameQueue.popElement(access))
		{
			FrameVector& curFrame(*access.getData());

			std::size_t numFilters = getNumFilters();

			// the size will be zero for a couple of frames, if there's some messing around with window sizes
			// or we get audio running before anything is actually initiated.
			if (curFrame.size() != 0)
			{
				if (curFrame.size() == numFilters)
				{
					postProcessTransform(reinterpret_cast<fpoint*>(curFrame.data()));
				}
				else
				{
					// linearly interpolate bins. if we win the cpu-lottery one day, change this to sinc.
					std::vector<std::complex<fpoint>> tempSpace(numFilters);

					// interpolation factor.
					fpoint wspToNext = (curFrame.size() - 1) / fpoint(std::max<std::size_t>(1, numFilters));

					for (std::size_t n = 0; n < numFilters; ++n)
					{
						auto y2 = n * wspToNext;
						auto x = static_cast<std::size_t>(y2);
						auto yFrac = y2 - x;
						tempSpace[n] = curFrame[x] * (fpoint(1) - yFrac) + curFrame[x + 1] * yFrac;
					}
					postProcessTransform(reinterpret_cast<fpoint *>(tempSpace.data()));
				}
			}

			return true;
		}
		return false;
	}

	struct AudioDispatcher
	{
		template<typename ISA> static void dispatch(Spectrum::ProcessorShell& shell, AudioStream::ListenerContext& source, AudioStream::DataType** buffer, std::size_t numChannels, std::size_t numSamples)
		{
			auto access = shell.streamState.lock();
			auto& state = access->Stream;
			state.audioEntryPoint<ISA>(access->Constant, source, buffer, numChannels, numSamples);

			if (access->Constant.displayMode == SpectrumContent::DisplayMode::ColourSpectrum)
			{
				for (std::size_t i = 0; i < state.sfbuf.size(); ++i)
				{
					Spectrum::SFrameQueue::ElementAccess access;

					// shouldn't be possible.
					if (!shell.frameQueue.acquireFreeElement<true, false>(access))
						break;

					auto& buffer = *access.getData();
					buffer = std::move(state.sfbuf[i]);
				}

				state.sfbuf.clear();
			}
		}
	};

	void Spectrum::ProcessorShell::onStreamAudio(AudioStream::ListenerContext& source, AudioStream::DataType** buffer, std::size_t numChannels, std::size_t numSamples)
	{
		if (isSuspended && globalBehaviour->stopProcessingOnSuspend)
			return;

		cpl::simd::dynamic_isa_dispatch<float, AudioDispatcher>(*this, source, buffer, numChannels, numSamples);
	}

	void Spectrum::ProcessorShell::onStreamPropertiesChanged(AudioStream::ListenerContext& source, const AudioStream::AudioStreamInfo& before)
	{
		auto access = streamState.lock();
		
		access->audioStreamChangeVersion.bump();
		access->streamLocalSampleRate = source.getInfo().sampleRate;
	}

	template<typename ISA>
	void Spectrum::StreamState::addAudioFrame(const Constant& constant)
	{
		mapToLinearSpace(constant);

		if (constant.algo == SpectrumContent::TransformAlgorithm::RSNT)
		{
			auto memory = getWorkingMemory<std::complex<fpoint>>();
			auto frame = FrameVector(memory, memory + constant.axisPoints /* channels ? */);
			sfbuf.emplace_back(std::move(frame));
		}
		else if (constant.algo == SpectrumContent::TransformAlgorithm::FFT)
		{
			// TODO: remove this once fft type == fpoint
			auto frame = FrameVector(constant.axisPoints);
			auto wsp = getWorkingMemory<std::complex<fftType>>();
			for (std::size_t i = 0; i < frame.size(); ++i)
			{
				frame[i].real = (fpoint)wsp[i].real();
				frame[i].imag = (fpoint)wsp[i].imag();
			}
			sfbuf.emplace_back(std::move(frame));
		}
	}

	template<typename V, class Vector>
	std::size_t Spectrum::StreamState::copyResonatorStateInto(const Constant& constant, cpl::dsp::WindowTypes windowType, Vector& output, std::size_t outChannels)
	{
		auto numResFilters = constant.Resonator.getNumFilters();
		// casts from std::complex<T> * to T * which is well-defined.
		// TODO: std::ranges
		auto buf = reinterpret_cast<fpoint*>(cpl::data(output));
		cresonator.getWholeWindowedState<V>(constant.Resonator, windowType, buf, outChannels, numResFilters);

		return numResFilters << (outChannels - 1);
	}

	Spectrum::ProcessorShell::ProcessorShell(std::shared_ptr<const SharedBehaviour>& behaviour)
		: globalBehaviour(behaviour), frameQueue(10)
	{
	}

	template<typename ISA>
	void Spectrum::StreamState::audioEntryPoint(const Constant& constant, AudioStream::ListenerContext& ctx, AudioStream::DataType** buffer, std::size_t numChannels, std::size_t numSamples)
	{ 
		if (constant.displayMode == SpectrumContent::DisplayMode::ColourSpectrum)
		{
			std::int64_t n = numSamples;
			std::size_t offset = 0;

			while (n > 0)
			{
				std::int64_t numRemainingSamples = currentCounter > constant.sampleBufferSize ? 0 : constant.sampleBufferSize - currentCounter;
				const auto availableSamples = numRemainingSamples + std::min(std::int64_t(0), n - numRemainingSamples);

				// do some resonation
				if (constant.algo == SpectrumContent::TransformAlgorithm::RSNT)
				{
					// TODO: Assumptions about channels...
					fpoint * offBuf[2] = { buffer[0] + offset, buffer[1] + offset };
					resonatingDispatch<ISA>(constant, offBuf, numChannels, availableSamples);
				}

				currentCounter += availableSamples;

				if (currentCounter >= constant.sampleBufferSize)
				{
					bool transformReady = true;
					if (constant.algo == SpectrumContent::TransformAlgorithm::FFT)
					{
						fpoint * offBuf[2] = { buffer[0], buffer[1]};
						if (ctx.getNumDeferredSamples() == 0)
						{
							// the abstract timeline consists of the old data in the audio stream, with the following audio presented in this function.
							// thus, the more we include of the buffer ('offbuf') the newer the data segment gets.
							if((transformReady = prepareTransform(constant, ctx.getAudioBufferViews(), offBuf, numChannels, availableSamples + offset)))
								doTransform(constant);
						}
						else
						{
							// ignore the deferred samples and produce some views that is slightly out-of-date.
							// this ONLY happens if something else is hogging the buffers.
							// TODO: missing offset?
							if((transformReady = prepareTransform(constant, ctx.getAudioBufferViews())))
								doTransform(constant);
						}
					}

					if(transformReady)
						addAudioFrame<ISA>(constant);

					currentCounter = 0;
				}

				offset += availableSamples;
				n -= availableSamples;
			}
		}
		else if(constant.algo == SpectrumContent::TransformAlgorithm::RSNT)
		{
			resonatingDispatch<ISA>(constant, buffer, numChannels, numSamples);
		}
		
	}


	template<typename ISA>
	void Spectrum::StreamState::resonatingDispatch(const Constant& constant, fpoint ** buffer, std::size_t numChannels, std::size_t numSamples)
	{
		// TODO: asserts?
		if (numChannels > 2)
			return;

		switch (constant.configuration)
		{
			case SpectrumChannels::Right:
			{
				cresonator.resonateReal<typename ISA::V>(constant.Resonator, &buffer[1], 1, numSamples);
				break;
			}
			case SpectrumChannels::Left:
			{
				cresonator.resonateReal<typename ISA::V>(constant.Resonator, buffer, 1, numSamples);
				break;
			}
			case SpectrumChannels::Mid:
			{
				ensureRelayBufferSize(1, numSamples);
				fpoint * rbuffer[] = { getRelayBufferChannel(0) };

				for (std::size_t i = 0; i < numSamples; ++i)
				{
					rbuffer[0][i] = fpoint(0.5) * (buffer[0][i] + buffer[1][i]);
				}

				cresonator.resonateReal<typename ISA::V>(constant.Resonator, rbuffer, 1, numSamples);
				break;
			}
			case SpectrumChannels::Side:
			{
				ensureRelayBufferSize(1, numSamples);
				fpoint * rbuffer[] = { getRelayBufferChannel(0) };

				for (std::size_t i = 0; i < numSamples; ++i)
				{
					rbuffer[0][i] = fpoint(0.5) * (buffer[0][i] - buffer[1][i]);
				}

				cresonator.resonateReal<typename ISA::V>(constant.Resonator, rbuffer, 1, numSamples);
				break;
			}
			case SpectrumChannels::MidSide:
			{
				ensureRelayBufferSize(numChannels, numSamples);
				fpoint * rbuffer[] = { getRelayBufferChannel(0), getRelayBufferChannel(1) };

				for (std::size_t i = 0; i < numSamples; ++i)
				{
					rbuffer[0][i] = buffer[0][i] + buffer[1][i];
					rbuffer[1][i] = buffer[0][i] - buffer[1][i];
				}

				cresonator.resonateReal<typename ISA::V>(constant.Resonator, rbuffer, 2, numSamples);
				break;
			}
			case SpectrumChannels::Phase:
			case SpectrumChannels::Separate:
			{
				cresonator.resonateReal<typename ISA::V>(constant.Resonator, buffer, 2, numSamples);
				break;
			}
			case SpectrumChannels::Complex:
			{
				cresonator.resonateComplex<typename ISA::V>(constant.Resonator, buffer, numSamples);
				break;
			}
		}
	}

	void Spectrum::StreamState::setStorage(const Constant& constant)
	{
		// some cases it is nice to have an extra entry (see handling of
		// separating real and imaginary transforms)
		// TODO: type these in terms of complex, not god damn chars.
		audioMemory.resize(constant.transformSize + 1);
		workingMemory.resize(constant.axisPoints * 2 * sizeof(std::complex<double>));
	}

	void Spectrum::StreamState::clearAudioState()
	{
		std::fill(workingMemory.begin(), workingMemory.end(), 0);
		std::fill(audioMemory.begin(), audioMemory.end(), 0);
		cresonator.resetState();
	}

	Spectrum::fpoint* Spectrum::StreamState::getRelayBufferChannel(std::size_t channel)
	{
		return relay.buffer.data() + channel * relay.samples;
	}

	void Spectrum::StreamState::ensureRelayBufferSize(std::size_t channels, std::size_t numSamples)
	{
		relay.buffer.resize(channels * numSamples);
		relay.samples = numSamples;
		relay.channels = channels;
	}

	float Spectrum::getSampleRate() const noexcept
	{
		return state.sampleRate;
	}

	int Spectrum::getAxisPoints() const noexcept
	{
		return static_cast<int>(state.axisPoints);
	}

	double Spectrum::getOptimalFramesPerUpdate() const noexcept
	{
#pragma message cwarn("collect this somewhere.")
		const double monitorRefreshRate = 60.0;
		auto res = double(isOpenGL() ? (monitorRefreshRate / getSwapInterval()) : refreshRate) / getBlobSamples();
		assert(std::isnormal(res));
		return res;
	}

	std::size_t Spectrum::getApproximateStoredFrames() const noexcept
	{
#pragma message cwarn("fix this to include channels, other processing methods.. etc.")
		return processor->frameQueue.enqueuededElements();
	}

	int Spectrum::getNumFilters() const noexcept
	{
		return getAxisPoints();
	}

	double Spectrum::getScallopingLossAtCoordinate(std::size_t coordinate, const Constant& constant)
	{
		auto ret = 0.6366; // default absolute worst case (equivalent to sinc(0.5), ie. rectangular windows

		if (state.axisPoints < 3)
			return ret;

		if (state.displayMode == SpectrumContent::DisplayMode::LineGraph)
		{
#pragma message cwarn("Fix this to use direct values")
			auto & value = content->dspWin;
			auto type = value.getWindowType();
			auto alpha = value.getAlpha();
			auto beta = value.getBeta();
			auto symmetry = value.getWindowShape();

			bool canOvershoot = false;
			auto sampleRate = getSampleRate();
			double normalizedBandwidth = 0, fractionateScallopLoss = normalizedBandwidth;


			auto safeIndex = cpl::Math::confineTo<std::size_t>(coordinate, 0, state.axisPoints - 2);
			if (state.algo == SpectrumContent::TransformAlgorithm::RSNT)
			{
				if (state.viewScale == SpectrumContent::ViewScaling::Linear)
				{

					normalizedBandwidth = state.windowSize * std::abs((double)constant.mapFrequency(safeIndex + 1) - constant.mapFrequency(safeIndex)) / sampleRate;

					normalizedBandwidth = std::min(0.5, normalizedBandwidth);
				}

				fractionateScallopLoss = cpl::dsp::windowScallopLoss(type, 4, normalizedBandwidth, cpl::dsp::Windows::Shape::Periodic, alpha, beta);
				// resonators have, per definition, at least 3 dB bandwidth, so number is equal to 10^(-3/20)
				fractionateScallopLoss = std::min(fractionateScallopLoss, 0.70794578438413791080221494218931);
			}
			else if (state.algo == SpectrumContent::TransformAlgorithm::FFT)
			{
				normalizedBandwidth = 0.5;

				if (state.binPolation == SpectrumContent::BinInterpolation::Lanczos && state.frequencyTrackingGraph != SpectrumContent::LineGraphs::Transform)
				{
					normalizedBandwidth = state.windowSize * std::abs((double)constant.mapFrequency(safeIndex + 1) - constant.mapFrequency(safeIndex)) / sampleRate;
					if (normalizedBandwidth < 0.5)
					{
						canOvershoot = true;
					}
					else
					{
						normalizedBandwidth = 0.5;
					}

				}

				fractionateScallopLoss = cpl::dsp::windowScallopLoss(type, 4, normalizedBandwidth, symmetry, alpha, beta);
			}

			ret = fractionateScallopLoss;
		}
		return ret;
	}
};
