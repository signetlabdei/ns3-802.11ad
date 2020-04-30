/*
 * Copyright (c) 2015-2019 IMDEA Networks Institute
 * Author: Hany Assasa <hany.assasa@gmail.com>
 */
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/wifi-module.h"
#include "common-functions.h"
#include <iomanip>
#include <sstream>

/**
 * Simulation Objective:
 * This script is used to evaluate the performance and behaviour of the IEEE 802.11ad standard in
 * an L-Shaped room scenario. In this scenario, the L corner blocks the LOS path and thus the STA
 * has to resort to NLOS (through a first order reflection) to resume communication. Both DMG AP
 * and DMG STA use a parametric codebook generated by our IEEE 802.11ad Codebook Generator in MATLAB.
 * Each device uses an URA antenna array of 2x8 Elements. The channel model is generated by our
 * Q-D Realization software.
 *
 * Network Topology:
 * Network topology is simple and consists of a single access point and a one DMG STA.
 * The DMG STA moves along the below trajectory and performs beamforming training each 10 BIs i.e 1 s.
 *
 *    ____________________
 *   | DMG AP             |
 *   |                    |
 *   |    |               |
 *   |    |               |
 *   |    |               |
 *   |    |               |
 *   |    |               |
 *   |    |               |
 *   |    |               |
 *   |    |               |_______________________________________________________________
 *   |    |                                                                               |
 *   |    |                                                                               |
 *   |    |                                                                               |
 *   |    |                                                                               |
 *   |    |                                                                               |
 *   |  DMG STA -------------------------------------------------------------------->     |
 *   |                                                                                    |
 *   |____________________________________________________________________________________|
 *
 *
 * Running the Simulation:
 * ./waf --run "evaluate_qd_channel_lroom_scenario"
 *
 * Simulation Output:
 * The simulation generates the following traces:
 * 1. PCAP traces for each station.
 * 2. SNR data for all the packets.
 * 3. Beamforming Traces.
 */

NS_LOG_COMPONENT_DEFINE ("Mobility");

using namespace ns3;
using namespace std;

/**  Application Variables **/
string applicationType = "onoff";                      /* Type of the Tx application */
string socketType = "ns3::UdpSocketFactory";           /* Socket Type (TCP/UDP) */
string schedulerType = "ns3::BasicDmgWifiScheduler";   /* Type of scheduler to be used */
uint64_t totalRx = 0;
double throughput = 0;
Ptr<PacketSink> packetSink;
Ptr<OnOffApplication> onoff;
Ptr<BulkSendApplication> bulk;
Time appStartTime = Seconds (0);
bool appStarted = false;

/* Network Nodes */
Ptr<WifiNetDevice> apWifiNetDevice;
Ptr<WifiNetDevice> staWifiNetDevice;
Ptr<WifiRemoteStationManager> apRemoteStationManager;
Ptr<DmgApWifiMac> apWifiMac;
Ptr<DmgStaWifiMac> staWifiMac;
Ptr<DmgWifiPhy> apWifiPhy;
Ptr<DmgWifiPhy> staWifiPhy;
Ptr<WifiRemoteStationManager> staRemoteStationManager;
Ptr<DmgWifiScheduler> dmgScheduler;
NetDeviceContainer staDevices;

/*** Beamforming TxSS Schedulling ***/
uint16_t biThreshold = 10;                /* BI Threshold to trigger TxSS TXOP. */
uint16_t biCounter;                       /* Number of beacon intervals that have passed. */

/* Flow monitor */
Ptr<FlowMonitor> monitor;

/* Statistics */
uint64_t macForwardUpBytes = 0;
uint64_t macTxDataFailed = 0;
uint64_t transmittedPackets = 0;
uint64_t droppedPackets = 0;
uint64_t receivedPackets = 0;
bool csv = false;                         /* Enable CSV output. */

/* Tracing */
Ptr<QdPropagationLossModel> lossModelRaytracing;                         //!< Q-D Channel Tracing Model.

std::vector<std::string>
SplitString (const std::string &str, char delimiter)
{
  std::stringstream ss (str);
  std::string token;
  std::vector<std::string> container;

  while (std::getline (ss, token, delimiter))
    {
      container.push_back (token);
    }
  return container;
}

void
EnableMyTraces (std::vector<std::string> &logComponents, Time tLogStart, Time tLogEnd)
{
  for (size_t i = 0; i < logComponents.size (); ++i)
    {
      const char* component = logComponents.at (i).c_str ();
      if (strlen(component) > 0)
        {
          NS_LOG_UNCOND ("Logging component " << component);
          Simulator::Schedule (tLogStart, &LogComponentEnable, component, LOG_LEVEL_ALL);
          Simulator::Schedule (tLogEnd, &LogComponentDisable, component, LOG_LEVEL_ALL);
        }
    }
}

struct Parameters : public SimpleRefCount<Parameters>
{
  uint32_t srcNodeID;
  uint32_t dstNodeID;
  Ptr<DmgWifiMac> wifiMac;
};

template <typename T>
std::string to_string_with_precision (const T a_value, const int n = 6)
{
  std::ostringstream out;
  out.precision (n);
  out << std::fixed << a_value;
  return out.str ();
}

double
CalculateSingleStreamThroughput (Ptr<PacketSink> sink, uint64_t &lastTotalRx, double &averageThroughput)
{
  double thr = (sink->GetTotalRx () - lastTotalRx) * (double) 8 / 1e5;     /* Convert Application RX Packets to MBits. */
  lastTotalRx = sink->GetTotalRx ();
  averageThroughput += thr;
  return thr;
}

void
CalculateThroughput (void)
{
  double thr = CalculateSingleStreamThroughput (packetSink, totalRx, throughput);
  if (!csv)
    {
      string duration = to_string_with_precision<double> (Simulator::Now ().GetSeconds () - 0.1, 1)
        + " - " + to_string_with_precision<double> (Simulator::Now ().GetSeconds (), 1);
      std::cout << std::left << std::setw (12) << duration
                << std::left << std::setw (12) << thr
                << std::left << std::setw (12) << lossModelRaytracing->GetCurrentTraceIndex () << std::endl;
    }
  else
    {
      std::cout << to_string_with_precision<double> (Simulator::Now ().GetSeconds (), 1) << "," << thr << std::endl;
    }
  Simulator::Schedule (MilliSeconds (100), &CalculateThroughput);
}

void
SLSCompleted (Ptr<OutputStreamWrapper> stream, Ptr<Parameters> parameters, Mac48Address address,  ChannelAccessPeriod accessPeriod,
              BeamformingDirection beamformingDirection, bool isInitiatorTxss, bool isResponderTxss,
              SECTOR_ID sectorId, ANTENNA_ID antennaId)
{
  *stream->GetStream () << parameters->srcNodeID + 1 << "," << parameters->dstNodeID + 1 << ","
                        << lossModelRaytracing->GetCurrentTraceIndex () << ","
                        << +sectorId << "," << +antennaId  << ","
                        << parameters->wifiMac->GetTypeOfStation ()  << ","
                        << apWifiNetDevice->GetNode ()->GetId () + 1  << ","
                        << Simulator::Now ().GetNanoSeconds () << std::endl;

  if (!csv)
    {
      NS_LOG_DEBUG ("DMG STA " << parameters->wifiMac->GetAddress () << " completed SLS phase with DMG STA " << address);
      NS_LOG_DEBUG ("Best Tx Antenna Configuration: SectorID=" << +sectorId << ", AntennaID=" << +antennaId);
    }
}

void
MacForwardUp (Ptr<Packet> packet, Mac48Address from, Mac48Address to)
{
  /* MAC layer throughput */
  macForwardUpBytes += packet->GetSize (); 
}

void
MacRxOk (Ptr<DmgWifiMac> wifiMac, Ptr<OutputStreamWrapper> stream,
         WifiMacType type, Ptr<const Packet> packet, Mac48Address address, double snrValue)
{
  if (type == WIFI_MAC_QOSDATA)
    {
      *stream->GetStream () << Simulator::Now ().GetNanoSeconds () << ","
                            << address << ","
                            << wifiMac->GetAddress () << ","
                            << snrValue << std::endl;
    }
}

void
CwTrace (uint32_t oldCw, uint32_t newCw)
{
  NS_LOG_DEBUG ("Old Cw: " << oldCw << ", New Cw: " << newCw);
}

void
CongStateTrace (TcpSocketState::TcpCongState_t oldState, TcpSocketState::TcpCongState_t newState)
{
  NS_LOG_DEBUG ("Old State: " << oldState << ", New State: " << newState); 
}

DmgTspecElement
GetDmgTspecElement (uint8_t allocId, bool isPseudoStatic, uint32_t minAllocation, uint32_t maxAllocation)
{
  /* Simple assert for the moment */
  NS_ASSERT_MSG (minAllocation <= maxAllocation, "Minimum Allocation cannot be greater than Maximum Allocation");
  NS_ASSERT_MSG (maxAllocation <= MAX_SP_BLOCK_DURATION, "Maximum Allocation exceeds Max SP block duration");
  DmgTspecElement element;
  DmgAllocationInfo info;
  info.SetAllocationID (allocId);
  info.SetAllocationType (SERVICE_PERIOD_ALLOCATION);
  info.SetAllocationFormat (ISOCHRONOUS);
  info.SetAsPseudoStatic (isPseudoStatic);
  info.SetDestinationAid (AID_AP);
  element.SetDmgAllocationInfo (info);
  element.SetMinimumAllocation (minAllocation);
  element.SetMaximumAllocation (maxAllocation);
  element.SetMinimumDuration (minAllocation);
  return element;
} 

void
StationAssociated (Ptr<DmgStaWifiMac> staWifiMac, Mac48Address address, uint16_t aid)
{
  if (!csv)
    {
      NS_LOG_DEBUG ("DMG STA " << staWifiMac->GetAddress () << " associated with DMG PCP/AP " << address
                    << ", AID= " << aid);
    }
    staWifiMac->CreateAllocation (GetDmgTspecElement (1, true, 1000, 1000));
    Simulator::Schedule (Seconds (1.0), &DmgStaWifiMac::CreateAllocation, staWifiMac, GetDmgTspecElement (1, true, 10000, 10000));
}

void
ADDTSResponseReceived (Mac48Address address, StatusCode status, DmgTspecElement element)
{
  NS_LOG_DEBUG (address << " Received ADDTS response with status: " << status.IsSuccess ());
  if ((status.IsSuccess () || (schedulerType == "ns3::CbapOnlyDmgWifiScheduler")) && !appStarted)
    {
      appStartTime = Simulator::Now ();
      appStarted = true;
      if (applicationType == "onoff")
        {
          onoff->StartApplication ();
        }
      else
        {
          bulk->StartApplication ();
        }
      /* Connect to TCP traces */
      if (socketType == "ns3::TcpSocketFactory")
        {
          if (applicationType == "onoff")
            {
              onoff->GetSocket ()->TraceConnectWithoutContext ("CongestionWindow", MakeCallback (&CwTrace));
              onoff->GetSocket ()->TraceConnectWithoutContext ("CongState", MakeCallback (&CongStateTrace));              
            }
          else
            {
              bulk->GetSocket ()->TraceConnectWithoutContext ("CongestionWindow", MakeCallback (&CwTrace));
              bulk->GetSocket ()->TraceConnectWithoutContext ("CongState", MakeCallback (&CongStateTrace));
            }
        }
    }
}

void
DataTransmissionIntervalStarted (Ptr<DmgApWifiMac> apWifiMac, Ptr<DmgStaWifiMac> staWifiMac, Mac48Address address, Time)
{
  if (apWifiMac->GetWifiRemoteStationManager ()->IsAssociated (staWifiMac->GetAddress ()) > 0)
    {
      biCounter++;
      if (biCounter == biThreshold)
        {
          staWifiMac->InitiateTxssCbap (address);
          biCounter = 0;
        }
    }
}

void
MacTxDataFailed (Mac48Address)
{
  macTxDataFailed++;
}

void
PhyTxEnd (Ptr<const Packet>)
{
  transmittedPackets++;
}

void
PhyRxDrop (Ptr<const Packet>)
{
  droppedPackets++;
}

void
PhyRxEnd (Ptr<const Packet>)
{
  receivedPackets++;
}


int
main (int argc, char *argv[])
{
  bool activateApp = true;                      /* Flag to indicate whether we activate onoff or bulk App */
  uint32_t packetSize = 1448;                   /* Application payload size in bytes. */
  string dataRate = "300Mbps";                  /* Application data rate. */
  string tcpVariant = "NewReno";                /* TCP Variant Type. */
  uint32_t bufferSize = 131072;                 /* TCP Send/Receive Buffer Size [bytes]. */
  uint32_t maxPackets = 0;                      /* Maximum Number of Packets */
  uint32_t msduAggregationSize = 7935;          /* The maximum aggregation size for A-MSDU [bytes]. */
  uint32_t mpduAggregationSize = 262143;        /* The maximum aggregation size for A-MSPU [bytes]. */
  uint32_t queueSize = 1000;                    /* Wifi MAC Queue Size. */
  string phyMode = "DMG_MCS12";                 /* Type of the Physical Layer. */
  uint16_t startDistance = 0;                   /* Starting distance in the Trace-File [0-260 m] */
  bool enableMobility = true;                   /* Enable mobility. */
  bool verbose = false;                         /* Print Logging Information. */
  double simulationTime = 10;                   /* Simulation time [s]. */
  bool pcapTracing = false;                     /* PCAP Tracing is enabled or not. */
  uint32_t interAllocDistance = 10;              /* Duration of a broadcast CBAP between two ADDTS allocations [us] */
  std::map<std::string, std::string> tcpVariants; /* List of the tcp Variants */
  uint16_t ac = 0;                              /* Select AC_BE as default AC */
  /*https://www.nsnam.org/doxygen/wifi-multi-tos_8cc_source.html */
  std::vector<uint8_t> tosValues = {0x70, 0x28, 0xb8, 0xc0}; /* AC_BE, AC_BK, AC_VI, AC_VO */
  std::string arrayConfig = "28";               /* Phased antenna array configuration*/
  std::string logComponentsStr = "";            /* Components to be logged from tLogStart to tLogEnd separated by ':' */
  double tLogStart = 0.0;                       /* Log start [s] */
  double tLogEnd = simulationTime;              /* Log end [s] */

  /** TCP Variants **/
  tcpVariants.insert (std::make_pair ("NewReno",       "ns3::TcpNewReno"));
  tcpVariants.insert (std::make_pair ("Hybla",         "ns3::TcpHybla"));
  tcpVariants.insert (std::make_pair ("HighSpeed",     "ns3::TcpHighSpeed"));
  tcpVariants.insert (std::make_pair ("Vegas",         "ns3::TcpVegas"));
  tcpVariants.insert (std::make_pair ("Scalable",      "ns3::TcpScalable"));
  tcpVariants.insert (std::make_pair ("Veno",          "ns3::TcpVeno"));
  tcpVariants.insert (std::make_pair ("Bic",           "ns3::TcpBic"));
  tcpVariants.insert (std::make_pair ("Westwood",      "ns3::TcpWestwood"));
  tcpVariants.insert (std::make_pair ("WestwoodPlus",  "ns3::TcpWestwoodPlus"));

  /* Command line argument parser setup. */
  CommandLine cmd;
  cmd.AddValue ("activateApp", "Whether to activate data transmission or not", activateApp);
  cmd.AddValue ("applicationType", "Type of the Tx Application: onoff or bulk", applicationType);
  cmd.AddValue ("packetSize", "Application packet size [bytes]", packetSize);
  cmd.AddValue ("dataRate", "Application data rate", dataRate);
  cmd.AddValue ("maxPackets", "Maximum number of packets to send", maxPackets);
  cmd.AddValue ("tcpVariant", "Transport protocol to use: TcpTahoe, TcpReno, TcpNewReno, TcpWestwood, TcpWestwoodPlus", tcpVariant);
  cmd.AddValue ("socketType", "Type of the Socket (ns3::TcpSocketFactory, ns3::UdpSocketFactory)", socketType);
  cmd.AddValue ("bufferSize", "TCP Buffer Size (Send/Receive) [bytes]", bufferSize);
  cmd.AddValue ("msduAggregation", "The maximum aggregation size for A-MSDU [bytes]", msduAggregationSize);
  cmd.AddValue ("mpduAggregation", "The maximum aggregation size for A-MPDU [bytes]", mpduAggregationSize);
  cmd.AddValue ("queueSize", "The maximum size of the Wifi MAC Queue", queueSize);
  cmd.AddValue ("phyMode", "802.11ad PHY Mode", phyMode);
  cmd.AddValue ("startDistance", "Starting distance in the trace file [0-260 m]", startDistance);
  cmd.AddValue ("biThreshold", "BI Threshold to trigger beamforming training", biThreshold);
  cmd.AddValue ("enableMobility", "Whether to enable mobility or simulate static scenario", enableMobility);
  cmd.AddValue ("verbose", "Turn on all WifiNetDevice log components", verbose);
  cmd.AddValue ("simulationTime", "Simulation time [s]", simulationTime);
  cmd.AddValue ("ac", "0: AC_BE, 1: AC_BK, 2: AC_VI, 3: AC_VO", ac);
  cmd.AddValue ("pcap", "Enable PCAP Tracing", pcapTracing);
  cmd.AddValue ("arrayConfig", "Antenna array configuration", arrayConfig);
  cmd.AddValue ("scheduler", "The type of scheduler to use in the simulation", schedulerType);
  cmd.AddValue ("interAllocation", "Duration of a broadcast CBAP between two ADDTS allocations [us]", interAllocDistance);
  cmd.AddValue ("csv", "Enable CSV output instead of plain text. This mode will suppress all the messages related statistics and events.", csv);
  cmd.AddValue ("logComponentsStr", "Components to be logged from tLogStart to tLogEnd separated by ':'", logComponentsStr);
  cmd.AddValue ("tLogStart", "Log start [s]", tLogStart);
  cmd.AddValue ("tLogEnd", "Log end [s]", tLogEnd);
  cmd.Parse (argc, argv);

  /* Global params: no fragmentation, no RTS/CTS, fixed rate for all packets */
  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("999999"));
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("999999"));
  Config::SetDefault ("ns3::QueueBase::MaxPackets", UintegerValue (queueSize));
  Config::SetDefault ("ns3::BasicDmgWifiScheduler::InterAllocationDistance", UintegerValue (interAllocDistance));

  std::vector<std::string> logComponents = SplitString (logComponentsStr, ':');
  EnableMyTraces (logComponents, Seconds (tLogStart), Seconds (tLogEnd));

  /*** Configure TCP Options ***/
  /* Select TCP variant */
  std::map<std::string, std::string>::const_iterator iter = tcpVariants.find (tcpVariant);
  NS_ASSERT_MSG (iter != tcpVariants.end (), "Cannot find Tcp Variant");
  TypeId tid = TypeId::LookupByName (iter->second);
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (tid));
  if (tcpVariant.compare ("Westwood") == 0)
    {
      Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOOD));
      Config::SetDefault ("ns3::TcpWestwood::FilterType", EnumValue (TcpWestwood::TUSTIN));
    }
  else if (tcpVariant.compare ("WestwoodPlus") == 0)
    {
      Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
      Config::SetDefault ("ns3::TcpWestwood::FilterType", EnumValue (TcpWestwood::TUSTIN));
    }

  /* Configure TCP Segment Size */
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (packetSize));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (bufferSize));
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (bufferSize));

  /**** DmgWifiHelper is a meta-helper ****/
  DmgWifiHelper wifi;

  /* Basic setup */
  wifi.SetStandard (WIFI_PHY_STANDARD_80211ad);

  /* Turn on logging */
  if (verbose)
    {
      LogComponentEnable ("Mobility", LOG_LEVEL_ALL);
      wifi.EnableDmgMacLogComponents ();
      wifi.EnableDmgPhyLogComponents ();
    }

  /**** Setup Ray-Tracing Channel ****/
  /**** Set up Channel ****/
  Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel> ();
  lossModelRaytracing = CreateObject<QdPropagationLossModel> ();
  Ptr<QdPropagationDelay> propagationDelayRayTracing = CreateObject<QdPropagationDelay> ();
  lossModelRaytracing->SetAttribute ("QDModelFolder", StringValue ("DmgFiles/QdChannel/L-ShapedRoom/"));
  propagationDelayRayTracing->SetAttribute ("QDModelFolder", StringValue ("DmgFiles/QdChannel/L-ShapedRoom/"));
  spectrumChannel->AddSpectrumPropagationLossModel (lossModelRaytracing);
  spectrumChannel->SetPropagationDelayModel (propagationDelayRayTracing);
  if (enableMobility)
    {
      lossModelRaytracing->SetAttribute ("Speed", DoubleValue (0.1));
      propagationDelayRayTracing->SetAttribute ("Speed", DoubleValue (0.1));
    }

  /**** Setup physical layer ****/
  SpectrumDmgWifiPhyHelper spectrumWifiPhy = SpectrumDmgWifiPhyHelper::Default ();
  spectrumWifiPhy.SetChannel (spectrumChannel);
  /* All nodes transmit at 10 dBm == 10 mW, no adaptation */
  spectrumWifiPhy.Set ("TxPowerStart", DoubleValue (10.0));
  spectrumWifiPhy.Set ("TxPowerEnd", DoubleValue (10.0));
  spectrumWifiPhy.Set ("TxPowerLevels", UintegerValue (1));
  /* Set operating channel */
  spectrumWifiPhy.Set ("ChannelNumber", UintegerValue (2));
  /* Sensitivity model includes implementation loss and noise figure */
  spectrumWifiPhy.Set ("CcaMode1Threshold", DoubleValue (-79));
  spectrumWifiPhy.Set ("EnergyDetectionThreshold", DoubleValue (-79 + 3));
  /* Custom error rate model for IEEE 802.11ad */
  spectrumWifiPhy.SetErrorRateModel ("ns3::DmgErrorModel",
                                     "FileName", StringValue ("DmgFiles/ErrorModel/LookupTable_1458.txt"));
  /* Set default algorithm for all nodes to be constant rate */
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "ControlMode", StringValue (phyMode),
                                "DataMode", StringValue (phyMode));
  /* Make four nodes and set them up with the phy and the mac */
  NodeContainer wifiNodes;
  wifiNodes.Create (2);
  Ptr<Node> apWifiNode = wifiNodes.Get (0);
  Ptr<Node> staWifiNode = wifiNodes.Get (1);

  /* Add a DMG upper mac */
  DmgWifiMacHelper wifiMac = DmgWifiMacHelper::Default ();

  /* Install DMG PCP/AP Node */
  Ssid ssid = Ssid ("Mobility");
  wifiMac.SetType ("ns3::DmgApWifiMac",
                   "Ssid", SsidValue (ssid),
                   "BE_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                   "BE_MaxAmsduSize", UintegerValue (msduAggregationSize),
                   "BK_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                   "BK_MaxAmsduSize", UintegerValue (msduAggregationSize),
                   "VI_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                   "VI_MaxAmsduSize", UintegerValue (msduAggregationSize),
                   "VO_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                   "VO_MaxAmsduSize", UintegerValue (msduAggregationSize));

  wifiMac.SetAttribute ("SSSlotsPerABFT", UintegerValue (8), "SSFramesPerSlot", UintegerValue (13),
                        "BeaconInterval", TimeValue (MicroSeconds (102400)),
                        "ATIPresent", BooleanValue (false));

  /* Set Parametric Codebook for the DMG AP */
  wifi.SetCodebook ("ns3::CodebookParametric",
                    "FileName", StringValue ("DmgFiles/Codebook/CODEBOOK_URA_AP_" + arrayConfig + "x.txt"));
  /* Set the Scheduler for the DMG AP */
  wifi.SetDmgScheduler (schedulerType);

  /* Create Wifi Network Devices (WifiNetDevice) */
  NetDeviceContainer apDevice;
  apDevice = wifi.Install (spectrumWifiPhy, wifiMac, apWifiNode);

  wifiMac.SetType ("ns3::DmgStaWifiMac",
                   "Ssid", SsidValue (ssid), "ActiveProbing", BooleanValue (false),
                   "BE_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                   "BE_MaxAmsduSize", UintegerValue (msduAggregationSize),
                   "BK_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                   "BK_MaxAmsduSize", UintegerValue (msduAggregationSize),
                   "VO_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                   "VO_MaxAmsduSize", UintegerValue (msduAggregationSize),
                   "VI_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                   "VI_MaxAmsduSize", UintegerValue (msduAggregationSize));

  /* Set Parametric Codebook for the DMG STA */
  wifi.SetCodebook ("ns3::CodebookParametric",
                    "FileName", StringValue ("DmgFiles/Codebook/CODEBOOK_URA_STA_" + arrayConfig + "x.txt"));

  staDevices = wifi.Install (spectrumWifiPhy, wifiMac, staWifiNode);

  /* Setting mobility model */
  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiNodes);

  /* Internet stack*/
  InternetStackHelper stack;
  stack.Install (wifiNodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterface;
  apInterface = address.Assign (apDevice);
  Ipv4InterfaceContainer staInterfaces;
  staInterfaces = address.Assign (staDevices);

  /* We do not want any ARP packets */
  PopulateArpCache ();

  if (activateApp)
    {
      /* Install Simple UDP Server on the DMG AP */
      PacketSinkHelper sinkHelper (socketType, InetSocketAddress (Ipv4Address::GetAny (), 9999));
      ApplicationContainer sinkApp = sinkHelper.Install (apWifiNode);
      packetSink = StaticCast<PacketSink> (sinkApp.Get (0));
      sinkApp.Start (Seconds (0.0));

      /* Install TCP/UDP Transmitter on the DMG STA */
      InetSocketAddress dest (InetSocketAddress (apInterface.GetAddress (0), 9999));
      dest.SetTos (tosValues.at (ac));
      ApplicationContainer srcApp;
      if (applicationType == "onoff")
        {
          OnOffHelper src (socketType, dest);
          src.SetAttribute ("MaxBytes", UintegerValue (maxPackets));
          src.SetAttribute ("PacketSize", UintegerValue (packetSize));
          src.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1e6]"));
          src.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
          src.SetAttribute ("DataRate", DataRateValue (DataRate (dataRate)));
          srcApp = src.Install (staWifiNode);
          onoff = StaticCast<OnOffApplication> (srcApp.Get (0));
        }
      else if (applicationType == "bulk")
        {
          BulkSendHelper src (socketType, dest);
          srcApp = src.Install (staWifiNode);
          bulk = StaticCast<BulkSendApplication> (srcApp.Get (0));
        }
      /* The application is started as soon as the STA is associated (when callback StationAssociated is called) */
      srcApp.Start (Seconds (simulationTime + 1));
      srcApp.Stop (Seconds (simulationTime));
    }

  /* Enable Traces */
  if (pcapTracing)
    {
      spectrumWifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO);
      spectrumWifiPhy.SetSnapshotLength (120);
      spectrumWifiPhy.EnablePcap ("Traces/AccessPoint", apDevice, false);
      spectrumWifiPhy.EnablePcap ("Traces/StaNode", staDevices.Get (0), false);
    }

  /* Stations */
  apWifiNetDevice = StaticCast<WifiNetDevice> (apDevice.Get (0));
  staWifiNetDevice = StaticCast<WifiNetDevice> (staDevices.Get (0));
  apRemoteStationManager = StaticCast<WifiRemoteStationManager> (apWifiNetDevice->GetRemoteStationManager ());
  apWifiMac = StaticCast<DmgApWifiMac> (apWifiNetDevice->GetMac ());
  staWifiMac = StaticCast<DmgStaWifiMac> (staWifiNetDevice->GetMac ());
  apWifiPhy = StaticCast<DmgWifiPhy> (apWifiNetDevice->GetPhy ());
  staWifiPhy = StaticCast<DmgWifiPhy> (staWifiNetDevice->GetPhy ());
  staRemoteStationManager = StaticCast<WifiRemoteStationManager> (staWifiNetDevice->GetRemoteStationManager ());

  /** Connect Traces **/
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> outputSlsPhase = ascii.CreateFileStream ("slsResults" + arrayConfig + ".csv");
  *outputSlsPhase->GetStream () << "SRC_ID,DST_ID,TRACE_IDX,SECTOR_ID,ANTENNA_ID,ROLE,BSS_ID,Timestamp" << std::endl;

  /* DMG AP Straces */
  Ptr<Parameters> parametersAp = Create<Parameters> ();
  parametersAp->srcNodeID = apWifiNetDevice->GetNode ()->GetId ();
  parametersAp->dstNodeID = staWifiNetDevice->GetNode ()->GetId ();
  parametersAp->wifiMac = apWifiMac;
  apWifiMac->TraceConnectWithoutContext ("ForwardUp", MakeCallback (&MacForwardUp));
  apWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, outputSlsPhase, parametersAp));
  apWifiMac->TraceConnectWithoutContext ("DTIStarted", MakeBoundCallback (&DataTransmissionIntervalStarted,
                                                                          apWifiMac, staWifiMac));
  apWifiPhy->TraceConnectWithoutContext ("PhyRxEnd", MakeCallback (&PhyRxEnd));
  apWifiPhy->TraceConnectWithoutContext ("PhyRxDrop", MakeCallback (&PhyRxDrop));

  /* DMG STA Straces */
  Ptr<Parameters> parametersSta = Create<Parameters> ();
  parametersSta->srcNodeID = staWifiNetDevice->GetNode ()->GetId ();
  parametersSta->dstNodeID = apWifiNetDevice->GetNode ()->GetId ();
  parametersSta->wifiMac = staWifiMac;
  staWifiMac->TraceConnectWithoutContext ("Assoc", MakeBoundCallback (&StationAssociated, staWifiMac));
  staWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, outputSlsPhase, parametersSta));
  staWifiMac->TraceConnectWithoutContext ("ADDTSResponse", MakeCallback (&ADDTSResponseReceived));
  staWifiPhy->TraceConnectWithoutContext ("PhyTxEnd", MakeCallback (&PhyTxEnd));
  staRemoteStationManager->TraceConnectWithoutContext ("MacTxDataFailed", MakeCallback (&MacTxDataFailed));

  /* Get SNR Traces */
  Ptr<OutputStreamWrapper> snrStream = ascii.CreateFileStream ("snrValues.csv");
  apRemoteStationManager->TraceConnectWithoutContext ("MacRxOK", MakeBoundCallback (&MacRxOk, apWifiMac, snrStream));

  FlowMonitorHelper flowmon;
  if (activateApp)
    {
      /* Install FlowMonitor on all nodes */
      monitor = flowmon.InstallAll ();

      /* Print Output */
      if (!csv)
        {
          std::cout << std::left << std::setw (12) << "Time [s]"
                    << std::left << std::setw (12) << "Throughput [Mbps]" << std::endl;
        }

      /* Schedule Throughput Calulcations */
      Simulator::Schedule (Seconds (0.1), &CalculateThroughput);
    }

  Simulator::Stop (Seconds (simulationTime + 0.101));
  Simulator::Run ();
  Simulator::Destroy ();

  if (!csv)
    {
      if (activateApp)
        {
          /* Print per flow statistics */
          monitor->CheckForLostPackets ();
          Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
          FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
          double simDuration = simulationTime - appStartTime.GetSeconds ();
          for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
            {
              Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
              std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
              std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
              std::cout << "  Tx Bytes:   " << i->second.txBytes << std::endl;
              std::cout << "  TxOffered:  " << i->second.txBytes * 8.0 / (simDuration * 1e6)  << " Mbps" << std::endl;
              std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
              std::cout << "  Rx Bytes:   " << i->second.rxBytes << std::endl;
              std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / (simDuration * 1e6)  << " Mbps" << std::endl;
            }

          /* Print Application Layer Results Summary */
          std::cout << "\nApplication Layer Statistics:" << std::endl;
          if (applicationType == "onoff")
            {
              std::cout << "  Tx Packets: " << onoff->GetTotalTxPackets () << std::endl;
              std::cout << "  Tx Bytes:   " << onoff->GetTotalTxBytes () << std::endl;
            }
          else
            {
              std::cout << "  Tx Packets: " << bulk->GetTotalTxPackets () << std::endl;
              std::cout << "  Tx Bytes:   " << bulk->GetTotalTxBytes () << std::endl;
            }
        }

      std::cout << "  Rx Packets: " << packetSink->GetTotalReceivedPackets () << std::endl;
      std::cout << "  Rx Bytes:   " << packetSink->GetTotalRx () << std::endl;
      std::cout << "  Throughput: " << packetSink->GetTotalRx () * 8.0 / ((simulationTime - appStartTime.GetSeconds ()) * 1e6) << " Mbps" << std::endl;
      std::cout << "  Avg Delay:  " << packetSink->GetAverageDelay ().GetSeconds () << " s" << std::endl;
      std::cout << "  Avg Delay:  " << packetSink->GetAverageDelay ().GetMicroSeconds () << " us" << std::endl;

      /* Print MAC Layer Statistics */
      std::cout << "\nMAC Layer Statistics:" << std::endl;
      std::cout << "  Mac Throughput: " << macForwardUpBytes * 8 / ((simulationTime - appStartTime.GetSeconds ()) * 1e6) << " Mbps" << std::endl;
      std::cout << "  Number of Failed Tx Data Packets:  " << macTxDataFailed << std::endl;

      /* Print PHY Layer Statistics */
      std::cout << "\nPHY Layer Statistics:" << std::endl;
      std::cout << "  Number of Tx Packets:         " << transmittedPackets << std::endl;
      std::cout << "  Number of Rx Packets:         " << receivedPackets << std::endl;
      std::cout << "  Number of Rx Dropped Packets: " << droppedPackets << std::endl;
    }

  return 0;
}
