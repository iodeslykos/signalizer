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

	file:Oscilloscope.h

		Interface for the oscilloscope parameters

*************************************************************************************/

#ifndef SIGNALIZER_COSCILLOSCOPEPARAMETERS_H
	#define SIGNALIZER_COSCILLOSCOPEPARAMETERS_H

	#include "Signalizer.h"

	namespace Signalizer
	{

		class OscilloscopeContent final
			: public cpl::Parameters::UserContent
			, public ProcessorStreamState<OscilloscopeContent>
			, public ParameterSet::UIListener
			, public std::enable_shared_from_this<OscilloscopeContent>
		{
		public:

			static constexpr std::size_t LookaheadSize = 8192;
			static constexpr std::size_t InterpolationKernelSize = 10;
			static constexpr std::size_t NumColourChannels = MaxInputChannels;

			enum class TriggeringMode
			{
				None,
				Spectral,
				Window,
				EnvelopeHold,
				ZeroCrossing,
				end
			};

			enum class TimeMode
			{
				Time, Cycles, Beats
			};

			enum class ColourMode
			{
				Static, SpectralEnergy
			};

			template<typename ParameterView>
			class WindowSizeTransformatter : public AudioHistoryTransformatter<ParameterView>
			{
			public:

			    typedef typename AudioHistoryTransformatter<ParameterView>::Mode Mode;
			    typedef typename AudioHistoryTransformatter<ParameterView>::ValueType ValueType;
			    typedef typename AudioHistoryTransformatter<ParameterView>::Scaling Scaling;

				WindowSizeTransformatter(std::shared_ptr<const ConcurrentConfig>& concurrentConfig, std::size_t auxLookahead, Mode mode = Mode::Milliseconds)
					: AudioHistoryTransformatter<ParameterView>(concurrentConfig, mode)
					, lookahead(auxLookahead)
					, timeMode(TimeMode::Time)
				{

				}

				void setTimeModeFromUI(TimeMode newMode)
				{
					timeMode = newMode;
					this->param->updateFromUINormalized(this->param->getValueNormalized());
				}

			private:

				virtual bool format(const ValueType & val, std::string & buf) override
				{
					char buffer[100];
					switch (timeMode)
					{
						case TimeMode::Cycles:
						{
							cpl::sprintfs(buffer, u8"%.2f (%.2f r)", val, cpl::simd::consts<ValueType>::tau * val);
							buf = buffer;
							return true;
						}
						case TimeMode::Beats:
						{
							cpl::sprintfs(buffer, "1/%.0f", val);
							buf = buffer;
							return true;
						}
						default: case TimeMode::Time: return AudioHistoryTransformatter<ParameterView>::format(val, buf);
					}
				}

				virtual bool interpret(const cpl::string_ref buf, ValueType & val) override
				{
					ValueType collectedValue;

					if (cpl::lexicalConversion(buf, collectedValue))
					{
						bool notSamples = true;
						if (timeMode != TimeMode::Time)
						{
							if (timeMode == TimeMode::Cycles)
							{
								if (buf.find("r") != std::string::npos)
								{
									collectedValue /= cpl::simd::consts<ValueType>::tau;
								}
							}
							else if (timeMode == TimeMode::Beats)
							{
								std::size_t numeratorEnd = 0;
								if ((numeratorEnd = buf.find('/')) != std::string::npos)
								{

									ValueType numerator, denominator;

									if (cpl::lexicalConversion(std::string(buf.begin() + numeratorEnd + 1, buf.end()), denominator)
										&& cpl::lexicalConversion(std::string(buf.begin(), buf.begin() + numeratorEnd), numerator))
									{
										collectedValue = numerator / denominator;
									}
									else
									{
										return false;
									}

								}

								if (buf.find("bars") != std::string::npos)
								{
									collectedValue /= 4;
								}
								// stored as reciprocal.
								collectedValue = 1 / collectedValue;
							}
							val = collectedValue;
							return true;
						}
						else if (buf.find("s") != std::string::npos && (notSamples = buf.find("smps") == std::string::npos))
						{
							if (buf.find("ms") != std::string::npos)
							{
								collectedValue /= 1000;
							}
							collectedValue *= this->sampleRate;
						}
						else
						{
							// assume value is in miliseconds
							if (this->m == Mode::Milliseconds && notSamples)
							{
								collectedValue /= 1000;
								collectedValue *= this->sampleRate;
							}
						}

						val = collectedValue;
						return true;

					}

					return false;

				}

				virtual ValueType transform(ValueType val) const noexcept override
				{
					switch (timeMode)
					{
						case TimeMode::Cycles:
						{
							return cpl::Math::UnityScale::exp<ValueType>(val, 1, 32);
						}
						case TimeMode::Beats:
						{
							return cpl::Math::nextPow2Inc(cpl::Math::round<std::size_t>(cpl::Math::UnityScale::exp<ValueType>(1 - val, 1, 32)));
						}
						default: case TimeMode::Time:
						{
							const auto minExponential = 100;
							const auto capacity = this->lastCapacity;

							const auto top = capacity;
							const auto expSamples = cpl::Math::UnityScale::exp<ValueType>(val, minExponential, top);
							const auto rescaled = cpl::Math::UnityScale::linear<ValueType>(cpl::Math::UnityScale::Inv::linear<ValueType>(expSamples, minExponential, top), 2, top);
							return rescaled;
						}
					}

				}


				virtual ValueType normalize(ValueType val) const noexcept override
				{
					switch (timeMode)
					{
						case TimeMode::Cycles:
						{
							return cpl::Math::UnityScale::Inv::exp<ValueType>(val, 1, 32);
						}
						case TimeMode::Beats:
						{
							return 1 - cpl::Math::UnityScale::Inv::exp<ValueType>(val, 1, 32);
						}
						default: case TimeMode::Time:
						{
							const auto minExponential = 100;
							const auto capacity = this->lastCapacity;
							const auto top = capacity;
							const auto linear = cpl::Math::UnityScale::Inv::linear<ValueType>(val, 2, top);
							const auto expSamples = cpl::Math::UnityScale::linear<ValueType>(linear, minExponential, top);

							const auto normalized = cpl::Math::UnityScale::Inv::exp<ValueType>(expSamples, minExponential, top);
							return normalized;
						}
					}
				}

				cpl::weak_atomic<Scaling> scale;
				std::size_t lookahead;
				TimeMode timeMode;
			};

			template<typename ParameterView>
			class LinearHzFormatter : public ParameterView::ParameterType::Formatter
			{
			public:

				typedef typename ParameterView::ParameterType::ValueType ValueType;

				LinearHzFormatter(const ConcurrentConfig& cfg)
					: config(cfg)
				{
					setTuningFromA4();
				}

				void setTuningFromA4(double hz = 440)
				{
					a4InHz = hz;
				}

				virtual bool format(const ValueType & val, std::string & buf) override
				{
					char buffer[100];
					cpl::sprintfs(buffer, "%.5f Hz", val);
					buf = buffer;
					return true;
				}

				virtual bool interpret(const cpl::string_ref buf, ValueType & val) override
				{
					ValueType contained;

					// try to parse it as a note firstly:
					int octave;
					char tone;
					char hash;
					static const std::pair<char, int> notes[] = { { 'a', 0 },{ 'b', 2 },{ 'c', -9 },{ 'd', -7 },{ 'e', -5 },{ 'f', -4 },{ 'g', -2 } };

					if (std::sscanf(buf.c_str(), "%c%d", &tone, &octave) == 2 && !isdigit(tone))
					{
						tone = tolower(tone);
						if (tone >= 'a' && tone <= 'g')
						{
							auto offset = notes[tone - 'a'].second;
							auto note = octave * 12 + offset;
							val = a4InHz * std::pow(2, (note - 48) / 12.0);
							return true;
						}
						else
						{
							return false;
						}
					}
					else if (std::sscanf(buf.c_str(), "%c%c%d", &tone, &hash, &octave) == 3 && !isdigit(tone))
					{
						tone = tolower(tone);
						if (tone >= 'a' && tone <= 'g')
						{
							auto offset = notes[tone - 'a'].second;
							if (hash == '#') offset++;
							else if (tolower(hash) == 'b') offset--;
							auto note = octave * 12 + offset;
							val = a4InHz * std::pow(2, (note - 48) / 12.0);
							return true;
						}
						else
						{
							return false;
						}
					}
					else if (cpl::lexicalConversion(buf, contained))
					{

						if (buf.find("smps") != std::string::npos)
						{
							contained = config.sampleRate / contained;
						}
						else if (buf.find("ms") != std::string::npos)
						{
							contained = 1.0 / (contained / 1000);
						}
						else if (buf.find("r") != std::string::npos)
						{
							contained = (contained / (2 * cpl::simd::consts<ValueType>::pi)) * config.sampleRate;
						}
						else if (buf.find("b") != std::string::npos)
						{
							contained = (contained * config.bpm) / 60;
						}

						val = contained;

						return true;
					}

					return false;
				}

			private:

				double a4InHz;
				const ConcurrentConfig& config;
			};

			static constexpr char* name = "Oscilloscope";

			static std::shared_ptr<ProcessorState> create(std::size_t parameterOffset, SystemView& system)
			{
				std::shared_ptr<OscilloscopeContent> ptr(new OscilloscopeContent(parameterOffset, system));
				return ptr;
			}

		private:

			OscilloscopeContent(std::size_t parameterOffset, SystemView& system)
				: concurrentConfig(system.getConcurrentConfig())
				, parameterSet(name, "OS.", system.getProcessor(), static_cast<int>(parameterOffset))
				, audioHistoryTransformatter(system.getConcurrentConfig(), LookaheadSize, WindowSizeTransformatter<ParameterSet::ParameterView>::Milliseconds)

				, dbRange(cpl::Math::dbToFraction(-120.0), cpl::Math::dbToFraction(120.0))
				, windowRange(0, 1000)
				, degreeRange(0, 360)
				, ptsRange(0.01, 10)
				, phaseRange(-180, 180)
				, reverseUnitRange(1, 0)
				, customTriggerRange(5, 48000)
				, colourSmoothRange(0.001, 1000)
				, triggerThresholdRange(0, 4)
				, triggerChannelRange(1, 16)
				, msFormatter("ms")
				, degreeFormatter("degs")
				, ptsFormatter("pts")
				, customTriggerFormatter(*concurrentConfig)

				, autoGain("AutoGain")
				, envelopeWindow("EnvWindow", windowRange, msFormatter)
				, inputGain("InputGain", dbRange, dbFormatter)
				, windowSize("WindowSize", audioHistoryTransformatter, audioHistoryTransformatter)
				, antialias("AntiAlias", boolRange, boolFormatter)
				, diagnostics("Diagnostics", boolRange, boolFormatter)
				, primitiveSize("PixelSize", ptsRange, ptsFormatter)
				, subSampleInterpolation("SampleIntp")
				, pctForDivision("PctDiv", unityRange, pctFormatter)
				, channelConfiguration("ChConf")
				, triggerPhaseOffset("TrgPhase", phaseRange, degreeFormatter)
				, triggerMode("TrgMode")
				, timeMode("TimeMode")
				, dotSamples("DotSmps", boolRange, boolFormatter)
				, triggerOnCustomFrequency("CustomTrg", boolRange, boolFormatter)
				, customTriggerFrequency("TrgFreq", customTriggerRange, customTriggerFormatter)
				, overlayChannels("Overlay", boolRange, boolFormatter)
				, channelColouring("Colouring")
				, colourSmoothing("ColSmooth", colourSmoothRange, msFormatter)
				, cursorTracker("CursorTrck", unityRange, boolFormatter)
				, showLegend("Show legend", boolRange, boolFormatter)
				, frequencyColouringBlend("FColBlend", unityRange, pctFormatter)
				, triggerHysteresis("TrgHstrs", unityRange, pctFormatter)
				, triggerThreshold("TrgThrhold", triggerThresholdRange, dbFormatter)
				, triggeringChannel("TrgChannel", triggerChannelRange, intFormatter) // triggerChannelFormatter

				, colourBehaviour()

				, primaryColour(colourBehaviour, "Prim" )
				, secondaryColour(colourBehaviour, "Sec" )
				, graphColour(colourBehaviour, "Graph.")
				, backgroundColour(colourBehaviour, "BackG.")
				, lowColour(colourBehaviour, "Low.")
				, midColour(colourBehaviour, "Mid.")
				, highColour(colourBehaviour, "High.")
				, widgetColour(colourBehaviour, "Widg.")
				, tsfBehaviour()
				, transform(tsfBehaviour)

			{
				viewOffsets.emplace_back("ViewLeft", unityRange, basicFormatter);
				viewOffsets.emplace_back("ViewTop", unityRange, basicFormatter);
				viewOffsets.emplace_back("ViewRight", reverseUnitRange, basicFormatter);
				viewOffsets.emplace_back("ViewBottom", reverseUnitRange, basicFormatter);

				autoGain.fmt.setValues({ "None", "RMS", "Peak decay" });
				subSampleInterpolation.fmt.setValues({ "None", "Rectangular", "Linear", "Lanczos" });
				channelConfiguration.fmt.setValues({ "Left", "Right", "Mid", "Side", "Separate", "Mid+Side"});
				triggerMode.fmt.setValues({ "None", "Spectral", "Window", "Envelope" , "Zero-crossing"});
				timeMode.fmt.setValues({ "Time", "Cycles", "Beats" });
				channelColouring.fmt.setValues({ "Static", "Spectral energy" });

				// order matters
				auto singleParameters = {
					&autoGain.param,
					&envelopeWindow,
					&inputGain,
					&windowSize,
					&antialias,
					&diagnostics,
					&primitiveSize,
					&subSampleInterpolation.param,
					&channelConfiguration.param,
					&pctForDivision,
					&triggerPhaseOffset,
					&triggerMode.param,
					&timeMode.param,
					&dotSamples,
					&triggerOnCustomFrequency,
					&customTriggerFrequency,
					&overlayChannels,
					&channelColouring.param,
					&colourSmoothing,
					&cursorTracker,
					&frequencyColouringBlend,
					&triggerHysteresis,
					&triggerThreshold,
					&triggeringChannel
				};

				for (auto sparam : singleParameters)
				{
					parameterSet.registerSingleParameter(sparam->generateUpdateRegistrator());
				}

				for (auto & v : viewOffsets)
				{
					parameterSet.registerSingleParameter(v.generateUpdateRegistrator());
				}

				for (auto cparam : { &primaryColour, &secondaryColour, &graphColour, &backgroundColour, &lowColour, &midColour, &highColour, &widgetColour })
				{
					parameterSet.registerParameterBundle(cparam, cparam->getBundleName());
				}

				parameterSet.registerParameterBundle(&transform, "3D.");

				// v. 0.3.6
				parameterSet.registerSingleParameter(showLegend.generateUpdateRegistrator());

				parameterSet.seal();
				postParameterInitialization();
				timeMode.param.getParameterView().addListener(this);
			}

		public:

			~OscilloscopeContent()
			{
				timeMode.param.getParameterView().removeListener(this);
			}

			void parameterChangedUI(cpl::Parameters::Handle localHandle, cpl::Parameters::Handle globalHandle, ParameterSet::ParameterView * parameter)
			{
				if (parameter == &timeMode.param.getParameterView())
				{
					audioHistoryTransformatter.setTimeModeFromUI(timeMode.param.getAsTEnum<TimeMode>());
				}
			}

			virtual const char* getName() override { return name; }

			virtual std::unique_ptr<cpl::CSubView> createView(
				std::shared_ptr<const SharedBehaviour>& globalBehaviour,
				std::shared_ptr<const ConcurrentConfig>& config,
				std::shared_ptr<AudioStream::Output>& stream
			) override;

			virtual std::unique_ptr<StateEditor> createEditor() override;

			virtual ParameterSet & getParameterSet() override
			{
				return parameterSet;
			}

			virtual void serialize(cpl::CSerializer::Archiver & archive, cpl::Version version) override
			{
				archive << windowSize;
				archive << inputGain;
				archive << antialias;
				archive << diagnostics;
				archive << graphColour;
				archive << backgroundColour;
				archive << primaryColour;
				archive << transform;
				archive << primitiveSize;
				archive << autoGain.param;
				archive << envelopeWindow;
				archive << subSampleInterpolation.param;
				archive << channelConfiguration.param;
				archive << pctForDivision;
				archive << triggerPhaseOffset;
				archive << triggerMode.param;
				archive << timeMode.param;
				for (auto && v : viewOffsets)
				{
					archive << v;
				}
				archive << dotSamples;
				archive << triggerOnCustomFrequency;
				archive << customTriggerFrequency;
				archive << overlayChannels;
				archive << channelColouring.param;
				archive << lowColour << midColour << highColour;
				archive << secondaryColour;
				archive << colourSmoothing;
				archive << cursorTracker;
				archive << widgetColour;
				archive << frequencyColouringBlend;
				archive << triggerHysteresis;
				archive << triggerThreshold;

				for (auto b = std::begin(extraColours); b != std::end(extraColours); ++b)
				{
					archive << *b;
				}

				archive << showLegend;
				archive << triggeringChannel;
			}

			virtual void deserialize(cpl::CSerializer::Builder & builder, cpl::Version version) override
			{
				builder >> windowSize;
				builder >> inputGain;
				builder >> antialias;
				builder >> diagnostics;
				builder >> graphColour;
				builder >> backgroundColour;
				builder >> primaryColour;
				builder >> transform;
				builder >> primitiveSize;
				builder >> autoGain.param;
				builder >> envelopeWindow;
				builder >> subSampleInterpolation.param;
				builder >> channelConfiguration.param;
				builder >> pctForDivision;
				builder >> triggerPhaseOffset;
				builder >> triggerMode.param;
				builder >> timeMode.param;

				for (auto & v : viewOffsets)
				{
					builder >> v;
				}

				builder >> dotSamples;
				builder >> triggerOnCustomFrequency;
				builder >> customTriggerFrequency;
				builder >> overlayChannels;
				builder >> channelColouring.param;
				builder >> lowColour >> midColour >> highColour;
				builder >> secondaryColour;
				builder >> colourSmoothing;

				if (version >= cpl::Version(0, 3, 1))
				{
					builder >> cursorTracker;
					builder >> widgetColour;
					builder >> frequencyColouringBlend;
				}

				if (version >= cpl::Version(0, 3, 2))
				{
					builder >> triggerHysteresis;
					builder >> triggerThreshold;
				}

				if (version >= cpl::Version(0, 3, 3))
				{
					for (auto b = std::begin(extraColours); b != std::end(extraColours); ++b)
					{
						builder >> *b;
					}

					builder >> showLegend;
					builder >> triggeringChannel;
				}
			}

			std::shared_ptr<const ConcurrentConfig> concurrentConfig;

			WindowSizeTransformatter<ParameterSet::ParameterView> audioHistoryTransformatter;
			LinearHzFormatter<ParameterSet::ParameterView> customTriggerFormatter;
			// TODO: if life time of parameters are extended, make this a weak pointer?
			ParameterSet parameterSet;

			cpl::UnitFormatter<double>
				msFormatter,
				degreeFormatter,
				ptsFormatter;

			cpl::PercentageFormatter<double>
				pctFormatter;

			cpl::DBFormatter<double> dbFormatter;
			cpl::BooleanFormatter<double> boolFormatter;

			cpl::BasicFormatter<double> basicFormatter;
			cpl::BooleanRange<double> boolRange;
			cpl::IntegerFormatter<double> intFormatter;

			cpl::ExponentialRange<double> dbRange, colourSmoothRange;

			cpl::LinearRange<double>
				ptsRange,
				windowRange,
				degreeRange,
				phaseRange,
				reverseUnitRange,
				customTriggerRange,
				triggerThresholdRange;

			cpl::IntegerLinearRange<double>
				triggerChannelRange;

			cpl::UnityRange<double> unityRange;

			enum ViewOffsets
			{
				Left, Top, Right, Bottom, end
			};

			cpl::ParameterValue<ParameterSet::ParameterView>
				envelopeWindow,
				inputGain,
				windowSize,
				antialias,
				diagnostics,
				primitiveSize,
				pctForDivision,
				triggerPhaseOffset,
				dotSamples,
				triggerOnCustomFrequency,
				customTriggerFrequency,
				overlayChannels,
				colourSmoothing,
				cursorTracker,
				frequencyColouringBlend,
				triggerHysteresis,
				triggerThreshold,
				triggeringChannel,
				showLegend;

			std::vector<cpl::ParameterValue<ParameterSet::ParameterView>> viewOffsets;

			ChoiceParameter
				autoGain,
				channelConfiguration,
				subSampleInterpolation,
				triggerMode,
				timeMode,
				channelColouring;

			cpl::ParameterColourValue<ParameterSet::ParameterView>::SharedBehaviour colourBehaviour;

			cpl::ParameterColourValue<ParameterSet::ParameterView>
				primaryColour,
				secondaryColour,
				graphColour,
				backgroundColour,
				lowColour, midColour, highColour,
				widgetColour;

			cpl::ParameterTransformValue<ParameterSet::ParameterView>::SharedBehaviour<ParameterSet::ParameterView::ValueType> tsfBehaviour;

			cpl::ParameterTransformValue<ParameterSet::ParameterView> transform;

			cpl::CompleteColour
				extraColours[NumColourChannels - 2];

		private:


			void onStreamPropertiesChanged(AudioStream::ListenerContext& changedSource, const AudioStream::AudioStreamInfo& before) override 
			{
				audioHistoryTransformatter.onStreamPropertiesChanged(changedSource, before);
			}

			void postParameterInitialization()
			{
				audioHistoryTransformatter.initialize(windowSize.getParameterView());
			}
		};

	};

#endif
