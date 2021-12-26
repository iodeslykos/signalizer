/*************************************************************************************

	Signalizer - cross-platform audio visualization plugin - v. 0.x.y

	Copyright (C) 2021 Janus Lynggaard Thorborg (www.jthorborg.com)

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

	file:HostGraph.h

		Live & serializable represenation of the running instances connected in the 
		host graph.

*************************************************************************************/


#ifndef SIGNALIZER_HOSTGRAPH_H
	#define SIGNALIZER_HOSTGRAPH_H

	#include <cpl/Common.h>
	#include <cpl/state/Serialization.h>
	#include <cpl/CAudioStream.h>
	#include <mutex>
	#include <shared_mutex>
	#include <map>
	#include <set>
	#include <string>
	#include <vector>
	#include <optional>
	#include "SignalizerConfiguration.h"
	#include "MixGraphListener.h"

	namespace Signalizer
	{

		class HostGraph : public cpl::CSerializer::Serializable
		{
		public:

			typedef int Version;
			static constexpr PinInt InvalidPin = -1;

			struct SerializedHandle
			{
				SerializedHandle(const SerializedHandle&) = default;
				SerializedHandle(SerializedHandle&&) = default;
				SerializedHandle& operator=(const SerializedHandle&) = default;
				SerializedHandle& operator=(SerializedHandle&&) = default;

				static SerializedHandle generateUnique()
				{
					return {};
				}

				friend inline bool operator == (const HostGraph::SerializedHandle& a, const HostGraph::SerializedHandle& b)
				{
					return !std::memcmp(a.Handle.getRawData(), b.Handle.getRawData(), 16);
				}

				friend inline bool operator < (const HostGraph::SerializedHandle& a, const HostGraph::SerializedHandle& b)
				{
					return std::memcmp(a.Handle.getRawData(), b.Handle.getRawData(), 16) < 0;
				}

				juce::String toString() { return Handle.toString(); }

			private:

				SerializedHandle() { }
				juce::Uuid Handle;
			};



			struct Model
			{
				struct NodeView
				{
					SerializedHandle node;
					std::string name;
					int connectionOffset;
					int connectionCount;
					int portCount;
					Version version;
				};

				bool didChange(NodeView& v)
				{
					return v.version >= previousVersion;
				}

				std::vector<NodeView> nodes;
				std::vector<DirectedPortPair> connections;
				int hostIndex{};
				
				Version previousVersion{};
			};


			HostGraph(std::shared_ptr<AudioStream>& realtime, AudioStream& presentation);
			~HostGraph();

			void addModelListener(std::weak_ptr<juce::AsyncUpdater> callback);

			void serialize(cpl::CSerializer::Archiver& ar, cpl::Version version) override;
			void deserialize(cpl::CSerializer::Builder& ar, cpl::Version version) override;

			Model getModel();
			void updateModel(Model& m);

			void setName(std::string_view newName);

			bool connect(const SerializedHandle& input, DirectedPortPair pair);
			bool disconnect(const SerializedHandle& input, DirectedPortPair pair);

		private:

			enum class DetailChange
			{
				Rename,
				Reidentified
			};

			typedef std::lock_guard<std::mutex> GraphLock;
			typedef HostGraph* HHandle;

			struct Relation
			{
				HHandle liveReference {};
				std::set<DirectedPortPair> inputs;
			};

			struct TriggerModelUpdateOnExit
			{
				TriggerModelUpdateOnExit(HostGraph* g) : graph(*g) {}

				HostGraph& graph;

				~TriggerModelUpdateOnExit()
				{
					if (auto e = graph.modelChangedCallback.lock())
					{
						e->triggerAsyncUpdate();
					}
				}
			};

			typedef std::pair<SerializedHandle, DirectedPortPair> SerializedEdge;
			typedef std::map<SerializedHandle, Relation> Topology;

			bool hasSerializedRepresentation() const;
			void submitConnect(HHandle h, DirectedPortPair pair, const GraphLock&);
			void submitDisconnect(HHandle h, DirectedPortPair pair, const GraphLock&);
			HHandle resolve(const SerializedHandle& h, const GraphLock&);
			void clearTopology(const GraphLock&);
			void tryRebuildTopology(HostGraph* other, const GraphLock&);
			// returns true if anything happend
			bool resetInstancedTopologyFor(const SerializedHandle& h, const GraphLock&, bool eraseSerializedInfo = false);

			static SerializedHandle serializeReference(HHandle h, const GraphLock&);
			HostGraph* resolve(HHandle h);
			void broadcastDetailChange(DetailChange change, const GraphLock&);
			void broadcastCreate(const GraphLock&);
			void broadcastDestruct(const GraphLock&);

			void onDetailChange(HHandle n, DetailChange change, const GraphLock&);
			void onNodeCreated(HHandle n, const GraphLock&);
			void onNodeDestroyed(HHandle n, const GraphLock&);

			static std::mutex staticMutex;
			static std::set<HHandle> staticSet;

			std::optional<SerializedHandle> nodeID;
			std::string name;
			std::vector<HHandle> pinInputs;
			Topology topology;
			std::weak_ptr<juce::AsyncUpdater> modelChangedCallback;
			MixGraphListener mix;
			std::size_t expectedNodesToResurrect = 0;
			int version = 0;
		};
	}

#endif