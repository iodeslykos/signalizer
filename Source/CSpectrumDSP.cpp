#include "CSpectrum.h"
#include <cpl/ffts.h>
#include <cpl/SysStats.h>
#include <cpl/lib/LockFreeDataQueue.h>
#include <cpl/stdext.h>
//#define _ZERO_PHASE

namespace Signalizer
{

	template<typename T>
		T * CSpectrum::getAudioMemory()
		{

			return reinterpret_cast<T*>(audioMemory.data());
		}

	std::size_t CSpectrum::getStateConfigurationChannels() const noexcept
	{
		return state.configuration > ChannelConfiguration::OffsetForMono ? 2 : 1;
	}

	template<typename T>
		std::size_t CSpectrum::getNumAudioElements() const noexcept
		{
			return audioMemory.size() / sizeof(T);
		}

	template<typename T>
		std::size_t CSpectrum::getFFTSpace() const noexcept
		{
			auto size = audioMemory.size();
			return size ? (size - 1) / sizeof(T) : 0;
		}

	template<typename T>
		T * CSpectrum::getWorkingMemory()
		{
			return reinterpret_cast<T*>(workingMemory.data());
		}

	template<typename T>
		std::size_t CSpectrum::getNumWorkingElements() const noexcept
		{
			return workingMemory.size() / sizeof(T);
		}

	std::size_t CSpectrum::getBlobSamples() const noexcept
	{
		return static_cast<std::size_t>(state.audioBlobSizeMs * 0.001 * getSampleRate());
	}

	void CSpectrum::resetState()
	{
		flags.resetStateBuffers = true;
	}


	void CSpectrum::computeWindowKernel()
	{
		size_t sampleSize = getWindowSize();
		std::size_t i = 0;


		switch (state.dspWindow)
		{

			case cpl::dsp::WindowTypes::Hann:
			{
				for (; i < sampleSize; ++i)
				{
					windowKernel[i] = 1.0 - std::cos(TAU * i / (sampleSize - 1));
				}
				break;
			}
			default:
				for (; i < sampleSize; ++i)
				{
					windowKernel[i] = 1.0;
				}
			break;
		}


		size_t fullSize = getFFTSpace<std::complex<double>>();
		// zero-padding
		for (; i < fullSize; ++i)
		{
			windowKernel[i] = 0.0;
		}
		// uncomment if you want to use window for convolution
		//cpl::dsp::CSignalTransform::sfft(reinterpret_cast<double*>(windowKernel.data()), fullSize);
	}



	void CSpectrum::prepareTransform(const AudioStream::AudioBufferAccess & audio)
	{

		auto size = getWindowSize(); // the size of the transform, containing samples
									 // the quantized (to next power of 2) samples of this transform
									 // that is, the size + additional zero-padding
		auto fullSize = getFFTSpace<std::complex<double>>();

		auto const channelConfiguration = kchannelConfiguration.getZeroBasedSelIndex<ChannelConfiguration>();


		{
			Stream::AudioBufferView views[2] = { audio.getView(0), audio.getView(1) };

			switch (state.algo)
			{
			case TransformAlgorithm::FFT:
			{
				auto buffer = getAudioMemory<std::complex<fftType>>();
				std::size_t channel = 1;
				std::size_t i = 0;

				switch (channelConfiguration)
				{
				case ChannelConfiguration::Left:
					channel = 0;
				case ChannelConfiguration::Right:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[channel].getItRange(indice);
						auto it = views[channel].getItIndex(indice);

						while (range--)
						{
							buffer[i] = *it++ * windowKernel[i];
							i++;
						}
					}
					break;
				}
				case ChannelConfiguration::Merge:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[0].getItRange(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);

						while (range--)
						{
							buffer[i] =	(*left++ + *right++) * windowKernel[i] * 0.5f;
							i++;
						}
					}
					break;
				}
				case ChannelConfiguration::Side:
				{

					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[0].getItRange(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);

						while (range--)
						{
							buffer[i] = (*left++ - *right++) * windowKernel[i] * 0.5f;
							i++;
						}
					}
					break;
				}
				case ChannelConfiguration::MidSide:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[0].getItRange(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);

						while (range--)
						{
							buffer[i] = std::complex<fftType>
							(
								(*left + *right) * windowKernel[i] * 0.5f,
								(*left - *right) * windowKernel[i] * 0.5f
							);
							left++;
							right++;
							i++;
						}
					}
					break;
				}
				case ChannelConfiguration::Phase:
				case ChannelConfiguration::Separate:
				{
					for (std::size_t indice = 0; indice < Stream::bufferIndices; ++indice)
					{
						std::size_t range = views[0].getItRange(indice);
						auto left = views[0].getItIndex(indice);
						auto right = views[1].getItIndex(indice);

						while (range--)
						{
							buffer[i] = std::complex<fftType>
							{
								*left++ * windowKernel[i],
								*right++ * windowKernel[i]
							};
							i++;
						}
					}
					break;
				}
				}
				//zero-pad until buffer is filled

				for (size_t pad = i; pad < fullSize; ++pad)
				{
					buffer[pad] = 0;
				}

				break;
			}
			/*// this case is different from CDFT, since the input musnt be windowed
			case Algorithm::MQDFT:
			{
				auto buffer = getAudioMemory<float>();
				size = getWindowSize();
				std::size_t channel = 1;
				winOsc.reset(size, 1, M_PI / 2); // offset phase by 90 degrees to create a cosine instead.
												 // this is used for reversing the index.
				auto const N = size - 1;
				switch (channelConfiguration)
				{
				case ChannelConfiguration::Left:
					channel = 0;
				case ChannelConfiguration::Right:
					for (unsigned i = 0; i < size; ++i)
					{
						buffer[N - i] = audioData[channel].singleCheckAccess(i);
					}
					break;
				case ChannelConfiguration::Phase:
				case ChannelConfiguration::Separate:
					for (unsigned n = 0; n < numChannels; ++n)
					{
						for (unsigned i = 0; i < size; ++i)
						{
							buffer[(N - i + n * size)] = audioData[n].singleCheckAccess(i);
						}
					}
					break;
				case ChannelConfiguration::Merge:
					for (unsigned i = 0; i < size; ++i)
					{
						buffer[N - i] = (audioData[0].singleCheckAccess(i) + audioData[1].singleCheckAccess(i)) * 0.5;
					}
					break;
				}
				break;
			}*/
			}
		}
	}

	void CSpectrum::prepareTransform(const AudioStream::AudioBufferAccess & audio, CSpectrum::fpoint ** preliminaryAudio, std::size_t numChannels, std::size_t numSamples)
	{

		auto size = getWindowSize(); // the size of the transform, containing samples
									 // the quantized (to next power of 2) samples of this transform
									 // that is, the size + additional zero-padding
		auto fullSize = getFFTSpace<std::complex<double>>();

		auto const channelConfiguration = kchannelConfiguration.getZeroBasedSelIndex<ChannelConfiguration>();


		{
			Stream::AudioBufferView views[2] = { audio.getView(0), audio.getView(1) };

			switch (state.algo)
			{
			case TransformAlgorithm::FFT:
			{
				auto buffer = getAudioMemory<std::complex<fftType>>();
				std::size_t channel = 1;
				std::size_t i = 0;
				std::size_t stop = std::min(numSamples, fullSize);

				switch (channelConfiguration)
				{
				case ChannelConfiguration::Left:
					channel = 0;
				case ChannelConfiguration::Right:
				{

					// process preliminary
					for (; i < stop; ++i)
					{
						buffer[i] = preliminaryAudio[channel][i] * windowKernel[i];
					}

					std::size_t offset = i;
					// get rest from buffers - first indice is a special case.

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

							while (range-- || i < stop)
							{
								buffer[i] = *it++ * windowKernel[i];
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
				case ChannelConfiguration::Merge:
				{
					for (; i < stop; ++i)
					{
						buffer[i] = (preliminaryAudio[0][i] + preliminaryAudio[1][i]) * windowKernel[i] * (fftType)0.5;
					}

					std::size_t offset = i;

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

							while (range-- || i < stop)
							{
								buffer[i] = (*left++ + *right++) * windowKernel[i] * 0.5f;
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
				case ChannelConfiguration::Side:
				{
					for (; i < stop; ++i)
					{
						buffer[i] = (preliminaryAudio[0][i] - preliminaryAudio[1][i]) * windowKernel[i] * (fftType)0.5;
					}

					std::size_t offset = i;

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

							while (range-- || i < stop)
							{
								buffer[i] = (*left++ - *right++) * windowKernel[i] * (fftType)0.5;
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
				case ChannelConfiguration::MidSide:
				{
					for (; i < stop; ++i)
					{
						buffer[i] = std::complex<fftType>
							(
								(preliminaryAudio[0][i] + preliminaryAudio[1][i]) * windowKernel[i] * (fftType)0.5,
								(preliminaryAudio[0][i] - preliminaryAudio[1][i]) * windowKernel[i] * (fftType)0.5
								);
					}

					std::size_t offset = i;

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

							while (range-- || i < stop)
							{
								buffer[i] = std::complex<fftType>
									(
										(*left + *right) * windowKernel[i] * (fftType)0.5,
										(*left - *right) * windowKernel[i] * (fftType)0.5
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
				case ChannelConfiguration::Phase:
				case ChannelConfiguration::Separate:
				{
					for (; i < stop; ++i)
					{
						buffer[i] = std::complex<fftType>
							(
								preliminaryAudio[0][i] * windowKernel[i],
								preliminaryAudio[1][i] * windowKernel[i]
								);
					}

					std::size_t offset = i;

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

							while (range-- || i < stop)
							{
								buffer[i] = std::complex<fftType>
								{
									*left++ * windowKernel[i],
									*right++ * windowKernel[i]
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

				for (size_t pad = i; pad < fullSize; ++pad)
				{
					buffer[pad] = 0;
				}

				break;
			}
			}
		}
	}

	void CSpectrum::doTransform()
	{
		auto const channelConfiguration = kchannelConfiguration.getZeroBasedSelIndex<ChannelConfiguration>();
		switch (state.algo)
		{
		case TransformAlgorithm::FFT:
		{
			auto const numSamples = getFFTSpace<std::complex<double>>();
			if(numSamples != 0)
				cpl::signaldust::DustFFT_fwdDa(getAudioMemory<double>(), numSamples);
			/*switch (channelConfiguration)
			{
			case ChannelConfiguration::Left:
			case ChannelConfiguration::Right:
			case ChannelConfiguration::Merge:
				cpl::signaldust::DustFFT_fwdDa(getAudioMemory<double>(), numSamples);
				break;
			case ChannelConfiguration::Phase:
			case ChannelConfiguration::Separate:
				cpl::signaldust::DustFFT_fwdDa(getAudioMemory<double>(), numSamples);

			}*/

			break;
		}
		/*case Algorithm::MQDFT:
		{
			auto size = getWindowSize();
			switch (channelConfiguration)
			{
			case ChannelConfiguration::Left:
			case ChannelConfiguration::Right:
			case ChannelConfiguration::Merge:
				transformer.mqdft<1>(getAudioMemory<float>(), size);
				break;
			case ChannelConfiguration::Phase:
			case ChannelConfiguration::Separate:
				transformer.mqdft<2>(getAudioMemory<float>(), size);

			}
			break;
		}
		// Resonators dont need preparation as they are continious and real time
		case Algorithm::RSNT:
			break;*/
		}
	}

	template<typename Ty>
		struct DualComplex
		{
			typedef Ty type;

			std::complex<type> val[2];

		};
	template<typename Ty>
		inline DualComplex<Ty> getZFromNFFT(Ty * tsf, std::size_t idx, std::size_t N)
		{
			idx <<= 1;
			N <<= 1;
			Ty x1 = tsf[idx];
			Ty x2 = tsf[N - idx];
			Ty y1 = tsf[idx + 1];
			Ty y2 = tsf[N - idx + 1];

			DualComplex<Ty> ret;
			//ret.val[0] = std::complex<Ty>((x1 + x2) * 0.5, (y1 + y2) * 0.5);
			//ret.val[1] = std::complex<Ty>((y1 - y2) * 0.5, -(x1 - x2) * 0.5);

			ret.val[0] = std::complex<Ty>((x1 + x2) * 0.5, (y1 - y2) * 0.5);
			ret.val[1] = std::complex<Ty>((y1 + y2) * 0.5, -(x1 - x2) * 0.5);

			return ret;
		}


	/*

		all inputs must be normalized.

		oldVals = state. First time must be zero. vector of floats of size. changed during call
		newVals = current vector of floats/doubles * 2 (complex), output from CSignalTransform::**dft() of size * 2
		output = vector of single floats, logarithmically mapped to 0 - 1 range, of size

		oldVals will be changed during the call.

			for mode = left/merge/mid/side/right
				newVals is a complex vector of floats of size
			for mode = separate, mid&side
				newVals is a complex vector of floats of size * 2
				newVals[n * 2 + 0] = lreal
				newVals[n * 2 + 1] = limag
				newVals[n * 2 + size + 0] = rreal
				newVals[n * 2 + size + 1] = rimag
			for mode = phase
				newVals[n * 2 + 0] = mag
				newVals[n * 2 + 1] = phase cancellation (with 1 being totally cancelled)

	*/
	template<class Scalar, class V2>
		void mapAndTransformDFTFilters(ChannelConfiguration type, UComplexFilter<Scalar> * __RESTRICT__ oldVals,
			const V2 & newVals, UComplexFilter<Scalar> * __RESTRICT__ output, std::size_t size,
			float lowDbs, float highDbs, float clip, cpl::CPeakFilter<Scalar> filter)
		{

			double lowerFraction = cpl::Math::dbToFraction<double>(lowDbs);
			double upperFraction = cpl::Math::dbToFraction<double>(highDbs);
			auto deltaYRecip = static_cast<Scalar>(1.0 / log(upperFraction / lowerFraction));
			auto minFracRecip = static_cast<Scalar>(1.0 / lowerFraction);
			auto halfRecip = Scalar(0.5);

			Scalar lowerClip = (Scalar)clip;

			switch (type)
			{
			case ChannelConfiguration::Left:
			case ChannelConfiguration::Merge:
			case ChannelConfiguration::Right:
			case ChannelConfiguration::Side:
			{

				for (cpl::Types::fint_t i = 0; i < size; ++i)
				{

					auto newReal = newVals[i * 2];
					auto newImag = newVals[i * 2 + 1];
					// mag = abs(cmplx)
					auto magnitude = sqrt(newReal * newReal + newImag * newImag);

					oldVals[i].magnitude *= filter.pole;

					if (magnitude > oldVals[i].magnitude)
					{
						oldVals[i].magnitude = (Scalar)magnitude;
					} 
					// log10(y / _min) / log10(_max / _min);

					auto deltaX = oldVals[i].magnitude * minFracRecip;
					// deltaX mostly zero here - add simd check
					auto result = deltaX > 0 ? log(deltaX) * deltaYRecip : lowerClip;
					output[i].magnitude = (Scalar)result;
					output[i].phase = 0;
				}
				break;
			}
			case ChannelConfiguration::Separate:
			case ChannelConfiguration::MidSide:
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

					oldVals[i].leftMagnitude *= filter.pole;
					oldVals[i].rightMagnitude *= filter.pole;

					if (lmag > oldVals[i].leftMagnitude)
					{
						oldVals[i].leftMagnitude = (Scalar)lmag;
					} 
					if (rmag > oldVals[i].rightMagnitude)
					{
						oldVals[i].rightMagnitude = (Scalar)rmag;
					}
					// log10(y / _min) / log10(_max / _min);
					auto deltaLX = oldVals[i].leftMagnitude * minFracRecip;
					auto deltaRX = oldVals[i].rightMagnitude * minFracRecip;
					// deltaX mostly zero here - add simd check
					auto lResult = deltaLX > 0 ? log(deltaLX) * deltaYRecip : lowerClip;
					auto rResult = deltaRX > 0 ? log(deltaRX) * deltaYRecip : lowerClip;
					output[i].leftMagnitude = (Scalar)lResult;
					output[i].rightMagnitude = (Scalar)rResult;
				}
				break;
			}
			case ChannelConfiguration::Phase:
			{
				auto const phaseFilter = std::pow(filter.pole, 0.3);
				for (cpl::Types::fint_t i = 0; i < size; ++i)
				{

					auto mag = newVals[i * 2];
					auto phase = newVals[i * 2 + 1];
					// mag = abs(cmplx)

					mag *= halfRecip;
					oldVals[i].magnitude *= filter.pole;

					if (mag > oldVals[i].magnitude)
					{
						oldVals[i].magnitude = (Scalar)mag;
					} 
					phase *= mag;

					oldVals[i].phase = phase + phaseFilter * (oldVals[i].phase - phase);


					// log10(y / _min) / log10(_max / _min);
					// deltaX mostly zero here - add simd check
					auto deltaX = oldVals[i].magnitude * minFracRecip;
					auto deltaY = oldVals[i].phase * minFracRecip;
					// deltaX mostly zero here - add simd check
					auto result = deltaX > 0 ? log(deltaX) * deltaYRecip : lowerClip;
					output[i].magnitude = (Scalar)result;
					output[i].phase = (Scalar)(deltaY > 0 ? log(deltaY) * deltaYRecip : lowerClip);
				}
				break;
			}
			};


		}

	template<class InVector>
		void CSpectrum::postProcessTransform(const InVector & transform, std::size_t size)
		{
			auto const & dbRange = getDBs();
			if (size > filterStates.size())
				CPL_RUNTIME_EXCEPTION("Incompatible incoming transform size.");
			mapAndTransformDFTFilters<fpoint>(state.configuration, filterStates.data(), transform, filterResults.data(), size, dbRange.low, dbRange.high, kMinDbs, peakFilter);
		}

	void CSpectrum::postProcessStdTransform()
	{
		if (state.algo == TransformAlgorithm::FFT)
			postProcessTransform(getWorkingMemory<fftType>(), filterStates.size());
		else
			postProcessTransform(getWorkingMemory<fpoint>(), filterStates.size());
	}

	std::size_t CSpectrum::mapToLinearSpace()
	{
		using namespace cpl;
		std::size_t numPoints = getAxisPoints();

		std::size_t numFilters = getNumFilters();

		switch (state.algo)
		{
		case TransformAlgorithm::FFT:
		{
			int bin = 0, oldBin = 0, maxLBin, maxRBin = 0;
			std::size_t N = getFFTSpace<std::complex<double>>();

			// we rely on mapping indexes, so we need N > 2 at least.
			if (N == 0)
				return 0;

			std::size_t numBins = N >> 1;
			auto const topFrequency = getSampleRate() / 2;
			auto const freqToBin = double(numBins) / topFrequency;
			auto const pointsToBin = double(numBins) / numPoints;

			typedef fftType ftype;

			std::complex<ftype> leftMax, rightMax;

			ftype maxLMag, maxRMag, newLMag, newRMag;

			// complex transform results, N + 1 size
			std::complex<ftype> * csf = getAudioMemory<std::complex<ftype>>();
			// buffer for single results, numPoints * 2 size
			ftype * wsp = getWorkingMemory<ftype>();
			// buffer for complex results, numPoints size
			std::complex<ftype> * csp = getWorkingMemory<std::complex<ftype>>();

			// this will make scaling correct regardless of amount of zero-padding
			// notice the 0.5: fft's of size 32 will output 16 for exact frequency bin matches,
			// so we halve the reciprocal scaling factor to normalize the size.
			auto const invSize = windowScale / (getWindowSize() * 0.5);

			switch (state.configuration)
			{
			case ChannelConfiguration::Left:
			case ChannelConfiguration::Right:
			case ChannelConfiguration::Merge:
			case ChannelConfiguration::Side:
			{
				oldBin = mappedFrequencies[0] * freqToBin;

				// the DC (0) and nyquist bin are NOT 'halved' due to the symmetric nature of the fft,
				// so halve these:
				csf[0] *= 0.5;
				csf[N >> 1] *= 0.5;

				for (std::size_t i = 0; i < numBins; ++i)
				{
					csf[i] = std::abs(csf[i]);
				}

				double fftBandwidth = 1.0 / numBins;
				//double pxlBandwidth = 1.0 / numPoints;
				cpl::Types::fint_t x = 0;
				switch (state.binPolation)
				{
				case BinInterpolation::None:
					for (x = 0; x < numPoints - 1; ++x)
					{
						// bandwidth of the filter for this 'line', or point
						double bwForLine = (mappedFrequencies[x + 1] - mappedFrequencies[x]) / topFrequency;
						// as long as the bandwidth is smaller than our fft resolution, we interpolate the points
						// otherwise, break out and sample the max values of the bins inside the bandwidth
						if (bwForLine > fftBandwidth)
							break;
						// +0.5 to centerly space bins.
						auto index = Math::confineTo((std::size_t)(mappedFrequencies[x] * freqToBin + 0.5), 0, numBins - 1);
						csp[x] = invSize * csf[index];
					}
					break;
				case BinInterpolation::Linear:
					for (x = 0; x < numPoints - 1; ++x)
					{
						double bwForLine = (mappedFrequencies[x + 1] - mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
							break;

						csp[x] = invSize * dsp::linearFilter<std::complex<ftype>>(csf, N, mappedFrequencies[x] * freqToBin);
					}
					break;
				case BinInterpolation::Lanczos:
					for (x = 0; x < numPoints - 1; ++x)
					{

						double bwForLine = (mappedFrequencies[x + 1] - mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
							break;

						csp[x] = invSize * dsp::lanczosFilter<std::complex<ftype>, true>(csf, N, mappedFrequencies[x] * freqToBin, 5);
					}
					break;
				default:
					break;
				}

				oldBin = mappedFrequencies[x] * freqToBin;

				for (; x < numPoints; ++x)
				{
					maxLMag = maxRMag = newLMag = newRMag = 0;

					bin = static_cast<std::size_t>(mappedFrequencies[x] * freqToBin);
#ifdef DEBUG
					if (bin > getNumAudioElements < std::complex < ftype >> ())
						CPL_RUNTIME_EXCEPTION("Corrupt frequency mapping!");
#endif
					maxRBin = maxLBin = bin;

					signed diff = bin - oldBin;
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
			case ChannelConfiguration::Phase:
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
				std::size_t bandWidthBreakingPoint = numPoints;

				double fftBandwidth = 1.0 / numBins;
				//double pxlBandwidth = 1.0 / numPoints;
				cpl::Types::fint_t x = 0;
				switch (state.binPolation)
				{
				case BinInterpolation::Linear:
				{
					// phase pass
					for (x = 0; x < numPoints - 1; ++x)
					{
						double bwForLine = (mappedFrequencies[x + 1] - mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
						{
							bandWidthBreakingPoint = x;
							break;
						}

						auto iLeft = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, mappedFrequencies[x] * freqToBin);
						auto iRight = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, N - (mappedFrequencies[x] * freqToBin));

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
						auto binPosition = mappedFrequencies[x] * freqToBin;

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
				case BinInterpolation::Lanczos:
				{

					const auto lanczosFilterSize = 5;


					for (x = 0; x < numPoints - 1; ++x)
					{

						double bwForLine = (mappedFrequencies[x + 1] - mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
						{
							bandWidthBreakingPoint = x;
							break;
						}

						auto iLeft = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, mappedFrequencies[x] * freqToBin, lanczosFilterSize);
						auto iRight = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, N - (mappedFrequencies[x] * freqToBin), lanczosFilterSize);

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
						auto binPosition = mappedFrequencies[x] * freqToBin;

						// normalize vectors we need to magnitude-interpolate.
						while ((binPosition + lanczosFilterSize) > normalizedPosition && x < bandWidthBreakingPoint - (lanczosFilterSize + 1))
						{
							csf[normalizedPosition] = std::abs(csf[normalizedPosition]);
							csf[N - normalizedPosition] = std::abs(csf[N - normalizedPosition]);
							normalizedPosition++;
						}

						auto iLeft = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, mappedFrequencies[x] * freqToBin, lanczosFilterSize);
						auto iRight = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, N - (mappedFrequencies[x] * freqToBin), lanczosFilterSize);

						wsp[x * 2] = invSize * (std::abs(iLeft) + std::abs(iRight));
					}

					break;
				}
				default:
					for (x = 0; x < numPoints - 1; ++x)
					{
						// bandwidth of the filter for this 'line', or point
						double bwForLine = (mappedFrequencies[x + 1] - mappedFrequencies[x]) / topFrequency;
						// as long as the bandwidth is smaller than our fft resolution, we interpolate the points
						// otherwise, break out and sample the max values of the bins inside the bandwidth
						if (bwForLine > fftBandwidth)
							break;

						// +0.5 to centerly space bins.
						auto index = Math::confineTo((std::size_t)(mappedFrequencies[x] * freqToBin + 0.5), 0, numBins - 1);
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
				if(x < numPoints)
					oldBin = mappedFrequencies[x] * freqToBin;



				for (; x < numPoints; ++x)
				{
					std::size_t maxBin = 0;
					ftype maxValue = 0, newMag = 0;
					bin = static_cast<std::size_t>(mappedFrequencies[x] * freqToBin);
#ifdef DEBUG
					if (bin > getNumAudioElements < std::complex < ftype >> ())
						CPL_RUNTIME_EXCEPTION("Corrupt frequency mapping!");
#endif

					signed diff = bin - oldBin;
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
			case ChannelConfiguration::Separate:
			case ChannelConfiguration::MidSide:
			{
				// two-for-one pass, first channel is 0... N/2 -1, second is N/2 .. N -1
				dsp::separateTransformsIPL(csf, N);

				// fix up DC and nyquist bins (see previous function documentation)
				csf[N] = csf[0].imag() * 0.5;
				csf[0] = csf[0].real() * 0.5;
				csf[N >> 1] *= 0.5;
				csf[(N >> 1) - 1] *= 0.5;

				for (std::size_t i = 1; i < N; ++i)
				{
					csf[i] = std::abs(csf[i]);
				}

				// The index of the transform, where the bandwidth is higher than mapped pixels (so no more interpolation is needed)
				// TODO: This can be calculated from view mapping scale and N pixels.
				std::size_t bandWidthBreakingPoint = numPoints - 1;

				double fftBandwidth = 1.0 / numBins;
				//double pxlBandwidth = 1.0 / numPoints;
				cpl::Types::fint_t x;
				switch (state.binPolation)
				{
				case BinInterpolation::Linear:

					// phase pass
					for (x = 0; x < numPoints - 1; ++x)
					{
						double bwForLine = (mappedFrequencies[x + 1] - mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
						{
							bandWidthBreakingPoint = x;
							break;
						}

						auto iLeft = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, mappedFrequencies[x] * freqToBin);
						auto iRight = dsp::linearFilter<std::complex<ftype>>(csf, N + 1, N - (mappedFrequencies[x] * freqToBin));
						
						csp[x] = invSize * iLeft;
						csp[numFilters + x] = invSize * iRight;
					}

					break;
				case BinInterpolation::Lanczos:
					for (x = 0; x < numPoints - 1; ++x)
					{

						double bwForLine = (mappedFrequencies[x + 1] - mappedFrequencies[x]) / topFrequency;
						if (bwForLine > fftBandwidth)
						{
							bandWidthBreakingPoint = x;
							break;
						}

						auto iLeft = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, mappedFrequencies[x] * freqToBin, 5);
						auto iRight = dsp::lanczosFilter<std::complex<ftype>, true>(csf, N + 1, N - (mappedFrequencies[x] * freqToBin), 5);
						
						csp[x] = invSize * iLeft;
						csp[numFilters + x] = invSize * iRight;
					}

					break;
				default:
					for (x = 0; x < numPoints - 1; ++x)
					{
						// bandwidth of the filter for this 'line', or point
						double bwForLine = (mappedFrequencies[x + 1] - mappedFrequencies[x]) / topFrequency;
						// as long as the bandwidth is smaller than our fft resolution, we interpolate the points
						// otherwise, break out and sample the max values of the bins inside the bandwidth
						if (bwForLine > fftBandwidth)
							break;

						// +0.5 to centerly space bins.
						auto index = Math::confineTo((std::size_t)(mappedFrequencies[x] * freqToBin + 0.5), 0, numBins - 1);

						csp[x] = invSize * csf[index];
						csp[numFilters + x] = invSize * csf[N - index];
					}
					break;
				}


				// the process after interpolation is much simpler, as we dont have to account
				// for wrongly interpolation of phase-mangled vectors.

				oldBin = mappedFrequencies[x] * freqToBin;

				for (; x < numPoints; ++x)
				{
					maxLMag = maxRMag = newLMag = newRMag = 0;

					bin = static_cast<std::size_t>(mappedFrequencies[x] * freqToBin);
#ifdef DEBUG
					if (bin > getNumAudioElements < std::complex < ftype >> ())
						CPL_RUNTIME_EXCEPTION("Corrupt frequency mapping!");
#endif
					maxRBin = maxLBin = bin;

					signed diff = bin - oldBin;
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
					csp[numFilters + x] = invSize * csf[maxRBin];
					oldBin = bin;
				}
			}
			break;
			}
			break;
		}
		/*case Algorithm::MQDFT:
		{
			auto result = transformer.getTransformResult();
			auto totalData = filterStates.size() * numChannels;

			switch (channelConfiguration)
			{
			case ChannelConfiguration::Left:
			case ChannelConfiguration::Right:
			case ChannelConfiguration::Merge:
				mapAndTransformDFTFilters<ChannelConfiguration::Left, float>(filterStates.data(), result.data(), filterResults.data(),
					filterStates.size(), dbRange.low, dbRange.high, flt);
				break;
			case ChannelConfiguration::Separate:
				mapAndTransformDFTFilters<ChannelConfiguration::Separate, float>(filterStates.data(), result.data(), filterResults.data(),
					filterStates.size(), dbRange.low, dbRange.high, flt);
				break;
			case ChannelConfiguration::Phase:
				mapAndTransformDFTFilters<ChannelConfiguration::Phase, float>(filterStates.data(), result.data(), filterResults.data(),
					filterStates.size(), dbRange.low, dbRange.high, flt);
				break;
			}
			break;
		}*/
		case TransformAlgorithm::RSNT:
		{

			std::complex<float> * wsp = getWorkingMemory<std::complex<float>>();
			std::size_t filtersPerChannel;
			{
				// locking, to ensure the amount of resonators doesn't change inbetween.
				cpl::CMutex lock(cresonator);
				filtersPerChannel = copyResonatorStateInto<fpoint>(wsp) / getStateConfigurationChannels();
			}

				
			switch (state.configuration)
			{
			case ChannelConfiguration::Phase:
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

		return numFilters;
	}


	bool CSpectrum::processNextSpectrumFrame()
	{
		SFrameBuffer::FrameVector * next;
		if (sfbuf.frameQueue.popElement(next))
		{
			SFrameBuffer::FrameVector & curFrame(*next);

			std::size_t numFilters = getNumFilters();

			// the size will be zero for a couple of frames, if there's some messing around with window sizes
			// or we get audio running before anything is actually initiated.
			if (curFrame.size() != 0)
			{
				if (curFrame.size() == numFilters)
				{
					postProcessTransform(reinterpret_cast<fpoint*>(curFrame.data()), numFilters);
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
					postProcessTransform(reinterpret_cast<fpoint *>(tempSpace.data()), numFilters);
				}
			}

#pragma message cwarn("OPERATOR DELETE OMG!!")
			delete next;
			return true;
		}
		return false;
	}

	void CSpectrum::mapFrequencies()
	{

		throw std::runtime_error("Dont do this");

	}
	
	bool CSpectrum::onAsyncAudio(const AudioStream & source, AudioStream::DataType ** buffer, std::size_t numChannels, std::size_t numSamples)
	{
		if (isSuspended)
			return false;
		auto const factor = std::is_same<AudioStream::DataType, float>::value ? 1 : 2;
		switch (cpl::simd::max_vector_capacity<float>())
		{
		case 8:
			audioProcessing<typename cpl::simd::vector_of<fpoint, 8 * 4 / (sizeof(fpoint))>::type>(buffer, numChannels, numSamples);
			break;
		case 4:
			audioProcessing<typename cpl::simd::vector_of<fpoint, 4 * 4 / (sizeof(fpoint))>::type>(buffer, numChannels, numSamples);
			break;
		case 1:
			audioProcessing<typename cpl::simd::vector_of<fpoint, factor * 4 / (sizeof(fpoint))>::type>(buffer, numChannels, numSamples);
			break;
		}
		return false;
	}

	template<typename V>
	void CSpectrum::addAudioFrame()
	{
		auto filters = mapToLinearSpace();

		if (state.algo == TransformAlgorithm::RSNT)
		{
			auto & frame = *(new SFrameBuffer::FrameVector(getWorkingMemory<std::complex<fpoint>>(), getWorkingMemory<std::complex<fpoint>>() + filters /* channels ? */));
			sfbuf.frameQueue.pushElement<true>(&frame);
		}
		else if (state.algo == TransformAlgorithm::FFT)
		{
			auto & frame = *(new SFrameBuffer::FrameVector(filters));
			auto wsp = getWorkingMemory<std::complex<fftType>>();
			for (std::size_t i = 0; i < frame.size(); ++i)
			{
				frame[i].real = (fpoint)wsp[i].real();
				frame[i].imag = (fpoint)wsp[i].imag();
			}
			sfbuf.frameQueue.pushElement<true>(&frame);

		}

		sfbuf.currentCounter = 0;
	}



	template<typename V, class Vector>
	std::size_t CSpectrum::copyResonatorStateInto(Vector & output)
	{
		auto numResFilters = cresonator.getNumFilters();
		auto numChannels = getStateConfigurationChannels();
		// casts from std::complex<T> * to T * which is well-defined.

		auto buf = (fpoint*)cpl::data(output);
		cresonator.getWholeWindowedState<V>(state.dspWindow, buf, numChannels, numResFilters);

		return numResFilters << (numChannels - 1);
	}

	template<typename V>
		void CSpectrum::audioProcessing(float ** buffer, std::size_t numChannels, std::size_t numSamples)
		{

			// rest only for resonators.
			if (state.displayMode == DisplayMode::ColourSpectrum)
			{
				cpl::CMutex lock(sfbuf);

				std::int64_t n = numSamples;
				std::size_t offset = 0;
				while (n > 0)
				{
					std::int64_t numRemainingSamples = sfbuf.sampleBufferSize - sfbuf.currentCounter;

					const auto availableSamples = numRemainingSamples + std::min(std::int64_t(0), n - numRemainingSamples);



					// do some resonation
					if (state.algo == TransformAlgorithm::RSNT)
					{
						fpoint * offBuf[2] = { buffer[0] + offset, buffer[1] + offset };
						resonatingDispatch<V>(offBuf, numChannels, availableSamples);
					}


					sfbuf.currentCounter += availableSamples;
					offset += availableSamples;
					if (sfbuf.currentCounter >= (sfbuf.sampleBufferSize))
					{
						if (state.algo == TransformAlgorithm::FFT)
						{
							fpoint * offBuf[2] = { buffer[0], buffer[1] };
							if (audioStream.getNumDeferredSamples() == 0)
							{
								prepareTransform(audioStream.getAudioBufferViews(), offBuf, numChannels, offset);
								doTransform();
							}
							else
							{
								// ignore the deferred samples and produce some views that is slightly out-of-date.
								// this ONLY happens if something else is hogging the buffers.
								prepareTransform(audioStream.getAudioBufferViews());
								doTransform();
							}
						}

						addAudioFrame<V>();
						// change this here. oh really?
						sfbuf.sampleBufferSize = getBlobSamples();
					}

					n -= availableSamples;
				}


				sfbuf.sampleCounter += numSamples;
			}
			else if(state.algo == TransformAlgorithm::RSNT)
			{
				resonatingDispatch<V>(buffer, numChannels, numSamples);
			}

			return;
		}


	template<typename V>
	void CSpectrum::resonatingDispatch(fpoint ** buffer, std::size_t numChannels, std::size_t numSamples)
	{
		// TODO: asserts?
		if (numChannels > 2)
			return;

		switch (state.configuration)
		{
			case ChannelConfiguration::Right:
			{
				cresonator.resonate<V>(&buffer[1], 1, numSamples);
				break;
			}
			case ChannelConfiguration::Left:
			{
				cresonator.resonate<V>(buffer, 1, numSamples);
				break;
			}
			case ChannelConfiguration::Mid:
			{
				ensureRelayBufferSize(1, numSamples);
				fpoint * rbuffer[] = { getRelayBufferChannel(0) };

				for (std::size_t i = 0; i < numSamples; ++i)
				{
					rbuffer[0][i] = fpoint(0.5) * (buffer[0][i] + buffer[1][i]);
				}

				cresonator.resonate<V>(rbuffer, 1, numSamples);
				break;
			}
			case ChannelConfiguration::Side:
			{
				ensureRelayBufferSize(1, numSamples);
				fpoint * rbuffer[] = { getRelayBufferChannel(0) };

				for (std::size_t i = 0; i < numSamples; ++i)
				{
					rbuffer[0][i] = fpoint(0.5) * (buffer[0][i] - buffer[1][i]);
				}

				cresonator.resonate<V>(rbuffer, 1, numSamples);
				break;
			}
			case ChannelConfiguration::MidSide:
			{
				ensureRelayBufferSize(numChannels, numSamples);
				fpoint * rbuffer[] = { getRelayBufferChannel(0), getRelayBufferChannel(1) };

				for (std::size_t i = 0; i < numSamples; ++i)
				{
					rbuffer[0][i] = buffer[0][i] + buffer[1][i];
					rbuffer[1][i] = buffer[0][i] - buffer[1][i];
				}

				cresonator.resonate<V>(rbuffer, 2, numSamples);
				break;
			}
			case ChannelConfiguration::Phase:
			case ChannelConfiguration::Separate:
			{
				cresonator.resonate<V>(buffer, 2, numSamples);
				break;
			}
		}
	}

	CSpectrum::fpoint * CSpectrum::getRelayBufferChannel(std::size_t channel)
	{
		return relay.buffer.data() + channel * relay.samples;
	}

	void CSpectrum::ensureRelayBufferSize(std::size_t channels, std::size_t numSamples)
	{
		relay.buffer.resize(channels * numSamples);
		relay.samples = numSamples;
		relay.channels = channels;
	}

	float CSpectrum::getSampleRate() const noexcept
	{
		return static_cast<float>(audioStream.getInfo().sampleRate);
	}

	int CSpectrum::getAxisPoints() const noexcept
	{
		return state.axisPoints;
	}

	double CSpectrum::getOptimalFramesPerUpdate() const noexcept
	{
#pragma message cwarn("collect this somewhere.")
		const double monitorRefreshRate = 60.0;
		auto res = double(isOpenGL() ? (monitorRefreshRate / getSwapInterval())  : refreshRate) / getBlobSamples();
		assert(std::isnormal(res));
		return res;
	}

	std::size_t CSpectrum::getApproximateStoredFrames() const noexcept
	{
#pragma message cwarn("fix this to include channels, other processing methods.. etc.")
		return sfbuf.frameQueue.enqueuededElements();
	}

	int CSpectrum::getNumFilters() const noexcept
	{
		return getAxisPoints();
	}
};
