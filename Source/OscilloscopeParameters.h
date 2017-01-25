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
 
	file:COscilloscope.h

		Interface for the oscilloscope parameters
 
*************************************************************************************/

#ifndef SIGNALIZER_COSCILLOSCOPEPARAMETERS_H
	#define SIGNALIZER_COSCILLOSCOPEPARAMETERS_H

	#include "CommonSignalizer.h"
	#include "SignalizerDesign.h"

	namespace Signalizer
	{

		class OscilloscopeContent final
			: public cpl::Parameters::UserContent
			, public ProcessorState
		{
		public:

			enum class TriggeringMode
			{
				None,
				Spectral,
				//ZeroCrossing,
				end
			};

			class OscilloscopeController 
				: public CContentPage
			{
			public:

				OscilloscopeController(OscilloscopeContent & parentValue)
					: parent(parentValue)
					, kantiAlias(&parentValue.antialias)
					, kdiagnostics(&parentValue.diagnostics)
					, kwindow(&parentValue.windowSize)
					, kgain(&parentValue.inputGain)
					, kprimitiveSize(&parentValue.primitiveSize)
					, kenvelopeSmooth(&parentValue.envelopeWindow)
					, kdrawingColour(&parentValue.drawingColour)
					, kgraphColour(&parentValue.graphColour)
					, kbackgroundColour(&parentValue.backgroundColour)
					, kskeletonColour(&parentValue.skeletonColour)
					, ktransform(&parentValue.transform)
					, kenvelopeMode(&parentValue.autoGain.param)
					, kpresets(&valueSerializer, "oscilloscope")
					, ksubSampleInterpolationMode(&parentValue.subSampleInterpolation.param)
					, kpctForDivision(&parentValue.pctForDivision)
					, kchannelConfiguration(&parentValue.channelConfiguration.param)
					, ktriggerPhaseOffset(&parentValue.triggerPhaseOffset)
					, ktriggerMode(&parentValue.triggerMode.param)
					, editorSerializer(
						*this, 
						[](auto & oc, auto & se, auto version) { oc.serializeEditorSettings(se, version); },
						[](auto & oc, auto & se, auto version) { oc.deserializeEditorSettings(se, version); }
					)
					, valueSerializer(
						*this,
						[](auto & oc, auto & se, auto version) { oc.serializeAll(se, version); },
						[](auto & oc, auto & se, auto version) { oc.deserializeAll(se, version); }
					)
				{
					initControls();
					initUI();
				}

				cpl::SafeSerializableObject & getEditorSO() override { return editorSerializer; }

				~OscilloscopeController()
				{
					notifyDestruction();
				}

				void initControls()
				{
					kwindow.bSetTitle("Window size");
					kgain.bSetTitle("Input gain");
					kgraphColour.bSetTitle("Graph colour");
					kbackgroundColour.bSetTitle("Backg. colour");
					kdrawingColour.bSetTitle("Drawing colour");
					kskeletonColour.bSetTitle("Skeleton colour");
					kprimitiveSize.bSetTitle("Primitive size");
					kenvelopeSmooth.bSetTitle("Env. window");
					ksubSampleInterpolationMode.bSetTitle("Sample interpolation");
					kpctForDivision.bSetTitle("Grid div. space");
					kchannelConfiguration.bSetTitle("Channel conf.");
					ktriggerMode.bSetTitle("Trigger mode");
					ktriggerPhaseOffset.bSetTitle("Trigger phase");

					// buttons n controls
					kantiAlias.setSingleText("Antialias");
					kantiAlias.setToggleable(true);
					kdiagnostics.setSingleText("Diagnostics");
					kdiagnostics.setToggleable(true);
					kenvelopeMode.bSetTitle("Auto-gain mode");

					// descriptions.
					kwindow.bSetDescription("The size of the displayed time window.");
					kgain.bSetDescription("How much the input (x,y) is scaled (or the input gain)" \
						" - additional transform that only affects the waveform, and not the graph");
					kantiAlias.bSetDescription("Antialiases rendering (if set - see global settings for amount). May slow down rendering.");
					kdrawingColour.bSetDescription("The main colour to paint with.");
					kgraphColour.bSetDescription("The colour of the graph.");
					kbackgroundColour.bSetDescription("The background colour of the view.");
					kdiagnostics.bSetDescription("Toggle diagnostic information in top-left corner.");
					kskeletonColour.bSetDescription("The colour of the box skeleton indicating the OpenGL camera clip box.");
					kprimitiveSize.bSetDescription("The size of the rendered primitives (eg. lines or points).");
					kenvelopeMode.bSetDescription("Monitors the audio stream and automatically scales the input gain such that it approaches unity intensity (envelope following).");
					kenvelopeSmooth.bSetDescription("Responsiveness (RMS window size) - or the time it takes for the envelope follower to decay.");
					ksubSampleInterpolationMode.bSetDescription("Controls how point samples are interpolated to wave forms");
					kpctForDivision.bSetDescription("The minimum amount of free space that triggers a recursed frequency grid division; smaller values draw more frequency divisions.");
					kchannelConfiguration.bSetDescription("Select how the audio channels are interpreted.");
					ktriggerMode.bSetDescription("Select a mode for triggering waveforms - i.e. syncing them to the grid");
					ktriggerPhaseOffset.bSetDescription("A custom +/- full-circle offset for the phase on triggering");
				}

				void initUI()
				{
					if (auto page = addPage("Settings", "icons/svg/gear.svg"))
					{
						if (auto section = new Signalizer::CContentPage::MatrixSection())
						{
							section->addControl(&ktransform, 0);
							page->addSection(section, "Transform");
						}
						if (auto section = new Signalizer::CContentPage::MatrixSection())
						{
							section->addControl(&kenvelopeMode, 0);
							section->addControl(&kenvelopeSmooth, 1);
							
							section->addControl(&kchannelConfiguration, 0);

							section->addControl(&kgain, 1);

							section->addControl(&kwindow, 0);
							section->addControl(&kpctForDivision, 1);


							page->addSection(section, "Utility");
						}
						if (auto section = new Signalizer::CContentPage::MatrixSection())
						{
							section->addControl(&ktriggerMode, 0);
							section->addControl(&ktriggerPhaseOffset, 1);

							page->addSection(section, "Triggering");
						}
					}

					if (auto page = addPage("Rendering", "icons/svg/brush.svg"))
					{
						if (auto section = new Signalizer::CContentPage::MatrixSection())
						{
							section->addControl(&kantiAlias, 0);
							section->addControl(&kdiagnostics, 1);
							page->addSection(section, "Options");
						}
						if (auto section = new Signalizer::CContentPage::MatrixSection())
						{							
							section->addControl(&kprimitiveSize, 0);
							section->addControl(&ksubSampleInterpolationMode, 1);

							section->addControl(&kdrawingColour, 0);
							section->addControl(&kgraphColour, 1);
							section->addControl(&kbackgroundColour, 0);
							section->addControl(&kskeletonColour, 1);

							page->addSection(section, "Look");
						}
					}

					if (auto page = addPage("Utility", "icons/svg/wrench.svg"))
					{
						if (auto section = new Signalizer::CContentPage::MatrixSection())
						{
							section->addControl(&kpresets, 0);
							page->addSection(section, "Presets");
						}
					}
				}

			private:

				void serializeEditorSettings(cpl::CSerializer::Archiver & archive, cpl::Version version)
				{
					archive << kwindow;
					archive << kgain;
					archive << kantiAlias;
					archive << kdiagnostics;
					archive << kgraphColour;
					archive << kbackgroundColour;
					archive << kdrawingColour;
					archive << ktransform;
					archive << kskeletonColour;
					archive << kprimitiveSize;
					archive << kenvelopeMode;
					archive << kenvelopeSmooth;
					archive << ksubSampleInterpolationMode;
					archive << kpctForDivision;
					archive << kchannelConfiguration;
					archive << kpctForDivision;
					archive << ktriggerPhaseOffset;
					archive << ktriggerMode;
				}

				void deserializeEditorSettings(cpl::CSerializer::Archiver & builder, cpl::Version version)
				{
					// in general, controls should never restore values. However, older versions
					// of Signalizer does exactly this, so to keep backwards-compatibility, we 
					// can obtain the preset values through this.
					cpl::Serialization::ScopedModifier m(cpl::CSerializer::Modifiers::RestoreValue, version < cpl::Version(0, 2, 8));
					builder << m;

					builder >> kwindow;
					builder >> kgain;
					builder >> kantiAlias;
					builder >> kdiagnostics;
					builder >> kgraphColour;
					builder >> kbackgroundColour;
					builder >> kdrawingColour;
					builder >> ktransform;
					builder >> kskeletonColour;
					builder >> kprimitiveSize;
					builder >> kenvelopeMode;
					builder >> kenvelopeSmooth;
					builder >> ksubSampleInterpolationMode;
					builder >> kpctForDivision;
					builder >> kchannelConfiguration;
					builder >> kpctForDivision;
					builder >> ktriggerPhaseOffset;
					builder >> ktriggerMode;
				}


				// entrypoints for completely storing values and settings in independant blobs (the preset widget)
				void serializeAll(cpl::CSerializer::Archiver & archive, cpl::Version version)
				{
					if (version < cpl::Version(0, 2, 8))
					{
						// presets from < 0.2.8 only store editor settings with values
						serializeEditorSettings(archive, version);
					}
					else
					{
						// store parameter and editor settings separately
						serializeEditorSettings(archive.getContent("Editor"), version);
						archive.getContent("Parameters") << parent;
					}

				}

				// entrypoints for completely storing values and settings in independant blobs (the preset widget)
				void deserializeAll(cpl::CSerializer::Builder & builder, cpl::Version version)
				{
					if (version < cpl::Version(0, 2, 8))
					{
						// presets from < 0.2.8 only store editor settings with values
						deserializeEditorSettings(builder, version);
					}
					else
					{
						// store parameter and editor settings separately
						deserializeEditorSettings(builder.getContent("Editor"), version);
						builder.getContent("Parameters") >> parent;
					}
				}

				cpl::CButton kantiAlias, kdiagnostics;
				cpl::CValueKnobSlider kwindow, kgain, kprimitiveSize, kenvelopeSmooth, kpctForDivision, ktriggerPhaseOffset;
				cpl::CColourControl kdrawingColour, kgraphColour, kbackgroundColour, kskeletonColour;
				cpl::CTransformWidget ktransform;
				cpl::CValueComboBox kenvelopeMode, ksubSampleInterpolationMode, kchannelConfiguration, ktriggerMode;
				cpl::CPresetWidget kpresets;

				OscilloscopeContent & parent;

				SSOSurrogate<OscilloscopeController>
					editorSerializer,
					valueSerializer;
			};

			OscilloscopeContent(std::size_t offset, bool shouldCreateShortNames, SystemView system)
				: systemView(system)
				, parameterSet("Oscilloscope", "OS.", system.getProcessor(), static_cast<int>(offset))
				, audioHistoryTransformatter(system.getAudioStream(), audioHistoryTransformatter.Milliseconds)

				, dbRange(cpl::Math::dbToFraction(-120.0), cpl::Math::dbToFraction(120.0))
				, windowRange(0, 1000)
				, degreeRange(0, 360)
				, ptsRange(0.01, 10)
				, phaseRange(-180, 180)

				, msFormatter("ms")
				, degreeFormatter("degs")
				, ptsFormatter("pts")

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

				, colourBehavior()
				, drawingColour(colourBehavior, "Draw.")
				, graphColour(colourBehavior, "Graph.")
				, backgroundColour(colourBehavior, "BackG.")
				, skeletonColour(colourBehavior, "Skelt.")

				, tsfBehaviour()
				, transform(tsfBehaviour)
			{
				autoGain.fmt.setValues({ "None", "RMS", "Peak decay" });
				subSampleInterpolation.fmt.setValues({ "None", "Rectangular", "Linear", "Lanczos 5" });
				channelConfiguration.fmt.setValues({ "Left", "Right", "Mid/Merge", "Side", "Separate", "Mid+Side"});
				triggerMode.fmt.setValues({ "None", "Spectral" /*, "Zero-crossings" */});

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
					&triggerMode.param
				};

				for (auto sparam : singleParameters)
				{
					parameterSet.registerSingleParameter(sparam->generateUpdateRegistrator());
				}

				parameterSet.registerParameterBundle(&drawingColour, drawingColour.getBundleName());
				parameterSet.registerParameterBundle(&graphColour, graphColour.getBundleName());
				parameterSet.registerParameterBundle(&backgroundColour, backgroundColour.getBundleName());
				parameterSet.registerParameterBundle(&skeletonColour, skeletonColour.getBundleName());
				parameterSet.registerParameterBundle(&transform, "3D.");

				parameterSet.seal();

				postParameterInitialization();
			}

			virtual std::unique_ptr<StateEditor> createEditor() override
			{
				return std::make_unique<OscilloscopeController>(*this);
			}

			virtual ParameterSet & getParameterSet() override
			{
				return parameterSet;
			}

			virtual void serialize(cpl::CSerializer::Archiver & archive, cpl::Version v) override
			{
				archive << windowSize;
				archive << inputGain;
				archive << antialias;
				archive << diagnostics;
				archive << graphColour;
				archive << backgroundColour;
				archive << drawingColour;
				archive << transform;
				archive << skeletonColour;
				archive << primitiveSize;
				archive << autoGain.param;
				archive << envelopeWindow;
				archive << subSampleInterpolation.param;
				archive << channelConfiguration.param;
				archive << pctForDivision;
				archive << triggerPhaseOffset;
				archive << triggerMode.param;
			}

			virtual void deserialize(cpl::CSerializer::Builder & builder, cpl::Version v) override
			{
				builder >> windowSize;
				builder >> inputGain;
				builder >> antialias;
				builder >> diagnostics;
				builder >> graphColour;
				builder >> backgroundColour;
				builder >> drawingColour;
				builder >> transform;
				builder >> skeletonColour;
				builder >> primitiveSize;
				builder >> autoGain.param;
				builder >> envelopeWindow;
				builder >> subSampleInterpolation.param;
				builder >> channelConfiguration.param;
				builder >> pctForDivision;
				builder >> triggerPhaseOffset;
				builder >> triggerMode.param;
			}

			AudioHistoryTransformatter<ParameterSet::ParameterView> audioHistoryTransformatter;
			SystemView systemView;
			ParameterSet parameterSet;

			cpl::UnitFormatter<double>
				msFormatter,
				degreeFormatter,
				ptsFormatter;

			cpl::PercentageFormatter<double>
				pctFormatter;

			cpl::DBFormatter<double> dbFormatter;
			cpl::BooleanFormatter<double> boolFormatter;

			cpl::BooleanRange<double> boolRange;

			cpl::ExponentialRange<double> dbRange;

			cpl::LinearRange<double>
				ptsRange,
				windowRange,
				degreeRange,
				phaseRange;

			cpl::UnityRange<double> unityRange;


			cpl::ParameterValue<ParameterSet::ParameterView>
				envelopeWindow,
				inputGain,
				windowSize,
				antialias,
				diagnostics,
				primitiveSize,
				pctForDivision,
				triggerPhaseOffset;

			ChoiceParameter
				autoGain,
				channelConfiguration,
				subSampleInterpolation,
				triggerMode;


			cpl::ParameterColourValue<ParameterSet::ParameterView>::SharedBehaviour colourBehavior;

			cpl::ParameterColourValue<ParameterSet::ParameterView>
				drawingColour,
				graphColour,
				backgroundColour,
				skeletonColour;

			cpl::ParameterTransformValue<ParameterSet::ParameterView>::SharedBehaviour<ParameterSet::ParameterView::ValueType> tsfBehaviour;

			cpl::ParameterTransformValue<ParameterSet::ParameterView> transform;

		private:

			void postParameterInitialization()
			{
				audioHistoryTransformatter.initialize(windowSize.getParameterView());
			}
		};
	
	};

#endif