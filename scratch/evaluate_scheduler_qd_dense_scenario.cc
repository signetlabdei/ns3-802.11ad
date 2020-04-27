/*
 * Copyright (c) 2015-2019 IMDEA Networks Institute
 * Copyright (c) 2020, University of Padova, Department of Information
 * Engineering, SIGNET Lab.
 *
 * Author: Hany Assasa <hany.assasa@gmail.com>
 *         Tommy Azzino <tommy.azzino@gmail.com>
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
 * This script is used to evaluate the performance and behaviour of a scheduling alogorithm and admission policy for IEEE 802.11ad.
 * This script is based on "evaluate_qd_dense_scenario_single_ap".
 *
 * Network Topology:
 * The network consists of a single AP in the center of a room surrounded by 10 DMG STAs.
 *
 *
 *                                 DMG STA (10)
 *
 *
 *
 *                  DMG STA (1)                     DMG STA (9)
 *
 *
 *
 *          DMG STA (2)                                     DMG STA (8)
 *
 *                                    DMG AP
 *
 *          DMG STA (3)                                     DMG STA (7)
 *
 *                                       
 *
 *                  DMG STA (4)                     DMG STA (6)
 *
 *                                     
 *
 *                                  DMG STA (5)
 *
 *
 * Requested Service Periods:
 * DMG STA (1) --> DMG STA (2)
 * DMG STA (3) --> DMG STA (4)
 * DMG STA (6) --> DMG STA (7)
 * DMG STA (8) --> DMG STA (9)
 * DMG STA (1) --> DMG AP
 * DMG AP      --> DMG STA (10) 
 *
 *
 * Running the Simulation:
 * ./waf --run "evaluate_scheduler_qd_dense_scenario"
 *
 * Simulation Output:
 * The simulation generates the following traces:
 *
 */


NS_LOG_COMPONENT_DEFINE ("EvaluateScheduler");

using namespace ns3;
using namespace std;

typedef map<Mac48Address, uint32_t> Mac2IdMap;
typedef Mac2IdMap::iterator Mac2IdMapI;
Mac2IdMap mac2IdMap;
Ptr<QdPropagationLossModel> lossModelRaytracing;                   

struct Parameters : public SimpleRefCount<Parameters>
{
  uint32_t srcNodeID;
  Ptr<DmgWifiMac> wifiMac;
};

/* Type definitions */
struct CommunicationPair
{
  Ptr<Application> srcApp;
  Ptr<PacketSink> packetSink;
  uint64_t totalRx = 0;
  double throughput = 0;
  uint64_t appDataRate;
  Time startTime;
};
typedef map<Ptr<Node>, CommunicationPair> CommunicationPairList;
typedef CommunicationPairList::iterator CommunicationPairListI;
typedef CommunicationPairList::const_iterator CommunicationPairListCI;

/** Simulation Arguments **/
string applicationType = "onoff";             /* Type of the Tx application */
string socketType = "ns3::UdpSocketFactory";  /* Socket Type (TCP/UDP) */
string schedulerType = "ns3::CbapOnlyDmgWifiScheduler";   /* Type of scheduler to be used */
string phyMode = "DMG_MCS12";                   /* The MCS to use at the Physical Layer. */
uint32_t packetSize = 1448;                   /* Application payload size [bytes]. */
string tcpVariant = "NewReno";                /* TCP Variant Type. */
uint32_t maxPackets = 0;                      /* Maximum Number of Packets */
uint32_t msduAggregationSize = 7935;          /* The maximum aggregation size for A-MSDU [bytes]. */
uint32_t mpduAggregationSize = 262143;        /* The maximum aggregation size for A-MPDU [bytes]. */
double simulationTime = 10;                   /* Simulation time [s]. */
bool csv = false;                             /* Enable CSV output. */
bool reportDataSnr = true;                    /* Report Data Packets SNR. */
uint8_t allocationId = 1;                     /* The allocation ID of the DMG Tspec to create */

/* NUmber of SP allocations */
uint16_t numSPs = 6;

/*** Beamforming CBAP ***/
uint16_t biThreshold = 10;                    /* BI Threshold to trigger TxSS TXOP. */
map<Mac48Address, uint16_t> biCounter;   /* Number of beacon intervals that have passed. */

/**  Applications **/
CommunicationPairList communicationPairList;  /* List of communicating devices. */

Ptr<DmgApWifiMac> apWifiMac;

template <typename T>
string to_string_with_precision (const T a_value, const int n = 6)
{
  ostringstream out;
  out.precision (n);
  out << fixed << a_value;
  return out.str ();
}

vector<string>
SplitString (const string &str, char delimiter)
{
  stringstream ss (str);
  string token;
  vector<string> container;

  while (getline (ss, token, delimiter))
    {
      container.push_back (token);
    }
  return container;
}

void
EnableMyTraces (vector<string> &logComponents, Time tLogStart, Time tLogEnd)
{
  for (size_t i = 0; i < logComponents.size (); ++i)
    {
      const char* component = logComponents.at (i).c_str ();
      if (strlen (component) > 0)
        {
          NS_LOG_UNCOND ("Logging component " << component);
          Simulator::Schedule (tLogStart, &LogComponentEnable, component, LOG_LEVEL_ALL);
          Simulator::Schedule (tLogEnd, &LogComponentDisable, component, LOG_LEVEL_ALL);
        }
    }
}

double
CalculateSingleStreamThroughput (Ptr<PacketSink> sink, uint64_t &lastTotalRx, double &averageThroughput)
{
  double thr = (sink->GetTotalRx () - lastTotalRx) * (double) 8 / 1e5;     /* Convert Application RX Packets to Mbits. */
  lastTotalRx = sink->GetTotalRx ();
  averageThroughput += thr;
  return thr;
}

void
CalculateThroughput (void)
{
  double totalThr = 0;
  double thr;
  if (!csv)
    {
      string duration = to_string_with_precision<double> (Simulator::Now ().GetSeconds () - 0.1, 1) +
                        " - " + to_string_with_precision<double> (Simulator::Now ().GetSeconds (), 1);
      cout << left << setw (12) << duration;

      for (CommunicationPairListI it = communicationPairList.begin (); it != communicationPairList.end (); ++it)
        {
          thr = CalculateSingleStreamThroughput (it->second.packetSink, it->second.totalRx, it->second.throughput);
          totalThr += thr;
          cout << left << setw (12) << thr;
        }
      cout << left << setw (12) << totalThr << endl;
    }
  else
    {
      cout << to_string_with_precision<double> (Simulator::Now ().GetSeconds (), 1);
      for (CommunicationPairListI it = communicationPairList.begin (); it != communicationPairList.end (); it++)
        {
          thr = CalculateSingleStreamThroughput (it->second.packetSink, it->second.totalRx, it->second.throughput);
          totalThr += thr;
          cout << ", " << thr;
        }
      cout << ", " << totalThr << endl;
    }
  Simulator::Schedule (MilliSeconds (100), &CalculateThroughput);
}

void
ADDTSResponseReceived (Ptr<Node> node, Mac48Address address, StatusCode status, DmgTspecElement element)
{
  NS_LOG_DEBUG ("Mac=" << address << " received ADDTS response with status=" << status.IsSuccess ());
  if (status.IsSuccess () || (schedulerType == "ns3::CbapOnlyDmgWifiScheduler"))
    {
      CommunicationPairListI it = communicationPairList.find (node);
      if (it != communicationPairList.end ())
        {
          it->second.startTime = Simulator::Now ();
          it->second.srcApp->StartApplication ();
        }
      else
        {
          NS_FATAL_ERROR ("Could not find application to start.");
        }
    }
}

uint32_t
ComputeServicePeriodDuration (const uint64_t &dataBitRate, const uint64_t &phyModeDataRate)
{
  NS_LOG_UNCOND (dataBitRate << " " << phyModeDataRate);
  double numberBIs = Seconds (1).GetMicroSeconds () / double (apWifiMac->GetBeaconInterval ().GetMicroSeconds ());
  uint32_t spDuration = ceil (dataBitRate / double (phyModeDataRate) / numberBIs * 1e6);
  return spDuration;
}

DmgTspecElement
GetDmgTspecElement (uint8_t allocId, bool isPseudoStatic, uint32_t minAllocation, uint32_t maxAllocation)
{
  /* Simple assert for the moment */
  NS_LOG_FUNCTION (+allocId << isPseudoStatic << minAllocation << maxAllocation);
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
StationAssociated (Ptr<Node> node, Ptr<DmgWifiMac> staWifiMac, Mac48Address address, uint16_t aid)
{
  if (!csv)
    {
      cout << "DMG STA " << staWifiMac->GetAddress () << " associated with DMG PCP/AP " << address
           << ", AID= " << aid << endl;
    }

  /* Send ADDTS request to the PCP/AP */
  CommunicationPairListI it = communicationPairList.find (node);
  if (it != communicationPairList.end ())
    {
      uint32_t spDuration = ComputeServicePeriodDuration (it->second.appDataRate, WifiMode (phyMode).GetDataRate ());
      StaticCast<DmgStaWifiMac> (staWifiMac)->CreateAllocation (GetDmgTspecElement (allocationId++, true, spDuration, spDuration));
    }
  else
    {
      NS_FATAL_ERROR ("Could not find application for this node.");
    }
}

void
StationDeAssociated (Ptr<Node> node, Ptr<DmgWifiMac> staWifiMac, Mac48Address address)
{
  if (!csv)
    {
      cout << "DMG STA " << staWifiMac->GetAddress () << " deassociated from DMG PCP/AP " << address << endl;
    }

  CommunicationPairListI it = communicationPairList.find (node);
  if (it != communicationPairList.end ())
    {
      it->second.srcApp->StopApplication ();
    }
  else
    {
      NS_FATAL_ERROR ("Could not find application to delete.");
    }
}

CommunicationPair
InstallApplications (Ptr<Node> srcNode, Ptr<Node> dstNode, Ipv4Address address, string appDataRate, uint8_t appNumber)
{
  CommunicationPair commPair;

  /* Install TCP/UDP Transmitter on the source node */
  Address dest (InetSocketAddress (address, 9000 + appNumber));
  ApplicationContainer srcApp;
  if (applicationType == "onoff")
    {
      OnOffHelper src (socketType, dest);
      src.SetAttribute ("MaxBytes", UintegerValue (maxPackets));
      src.SetAttribute ("PacketSize", UintegerValue (packetSize));
      src.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1e6]"));
      src.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      src.SetAttribute ("DataRate", DataRateValue (DataRate (appDataRate)));
      srcApp = src.Install (srcNode);
    }
  else if (applicationType == "bulk")
    {
      BulkSendHelper src (socketType, dest);
      srcApp = src.Install (srcNode);
    }
  srcApp.Stop (Seconds (simulationTime));
  commPair.srcApp = srcApp.Get (0);
  commPair.appDataRate = DataRate (appDataRate).GetBitRate ();

  /* Install Simple TCP/UDP Server on the destination node */
  PacketSinkHelper sinkHelper (socketType, InetSocketAddress (Ipv4Address::GetAny (), 9000 + appNumber));
  ApplicationContainer sinkApp = sinkHelper.Install (dstNode);
  commPair.packetSink = StaticCast<PacketSink> (sinkApp.Get (0));
  sinkApp.Start (Seconds (0.0));

  return commPair;
}

void
SLSCompleted (Ptr<OutputStreamWrapper> stream, Ptr<Parameters> parameters,
              Mac48Address address, ChannelAccessPeriod accessPeriod,
              BeamformingDirection beamformingDirection, bool isInitiatorTxss, bool isResponderTxss,
              SECTOR_ID sectorId, ANTENNA_ID antennaId)
{
  // In the Visualizer, node ID takes into account the AP so +1 is needed
  *stream->GetStream () << parameters->srcNodeID + 1 << "," << mac2IdMap[address] + 1 << ","
                        << lossModelRaytracing->GetCurrentTraceIndex () << ","
                        << uint16_t (sectorId) << "," << uint16_t (antennaId)  << ","
                        << parameters->wifiMac->GetTypeOfStation ()  << ","
                        << mac2IdMap[parameters->wifiMac->GetBssid ()] + 1  << ","
                        << Simulator::Now ().GetNanoSeconds () << std::endl;
  if (!csv)
    {
      cout << "DMG STA: " << parameters->srcNodeID << " Address: " << address << " Sector ID: " << +sectorId
           << " Antenna ID: " << +antennaId << endl;
    }
}

void
DataTransmissionIntervalStarted (Ptr<DmgStaWifiMac> wifiMac, Mac48Address address, Time)
{
  if (wifiMac->IsAssociated ())
    {
      uint16_t counter = biCounter[address];
      counter++;
      if (counter == biThreshold)
        {
          wifiMac->InitiateTxssCbap (wifiMac->GetBssid ());
          counter = 0;
        }
      biCounter[address] = counter;
    }
}

void
MacRxOk (Ptr<DmgWifiMac> WifiMac, Ptr<OutputStreamWrapper> stream,
         WifiMacType type, Mac48Address address, double snrValue)
{
  if ((type == WIFI_MAC_QOSDATA) && reportDataSnr)
    {
      *stream->GetStream () << Simulator::Now ().GetNanoSeconds () << ","
                            << address << ","
                            << WifiMac->GetAddress () << ","
                            << snrValue << std::endl;
    }
  else if ((type == WIFI_MAC_EXTENSION_DMG_BEACON) || (type == WIFI_MAC_CTL_DMG_SSW)
           || (type == WIFI_MAC_CTL_DMG_SSW_FBCK) || (type == WIFI_MAC_CTL_DMG_SSW_ACK))
    {
      *stream->GetStream () << Simulator::Now ().GetNanoSeconds () << ","
                            << address << ","
                            << WifiMac->GetAddress () << ","
                            << snrValue << std::endl;
    }
}

int
main (int argc, char *argv[])
{
  uint32_t bufferSize = 131072;                   /* TCP Send/Receive Buffer Size [bytes]. */
  uint32_t queueSize = 1000;                      /* Wifi MAC Queue Size [packets]. */
  string appDataRate = "300Mbps";                    /* Application data rate. */
  bool frameCapture = false;                      /* Use a frame capture model. */
  double frameCaptureMargin = 10;                 /* Frame capture margin [dB]. */
  uint32_t snapShotLength = numeric_limits<uint32_t>::max (); /* The maximum PCAP Snapshot Length. */
  bool verbose = false;                           /* Print Logging Information. */
  bool pcapTracing = false;                       /* Enable PCAP Tracing. */
  uint16_t numSTAs = 10;                          /* The number of DMG STAs. */
  map<string, string> tcpVariants;                /* List of the TCP Variants */
  string qdChannelFolder = "DenseScenario";       /* The name of the folder containing the QD-Channel files. */
  string logComponentsStr = "";                   /* Components to be logged from tLogStart to tLogEnd separated by ':' */
  double tLogStart = 0.0;                         /* Log start [s] */
  double tLogEnd = simulationTime;                /* Log end [s] */
  string appDataRateStr = "";                     /* List of App Data Rates for each SP allocation separated by ':' */

  /** TCP Variants **/
  tcpVariants.insert (make_pair ("NewReno",       "ns3::TcpNewReno"));
  tcpVariants.insert (make_pair ("Hybla",         "ns3::TcpHybla"));
  tcpVariants.insert (make_pair ("HighSpeed",     "ns3::TcpHighSpeed"));
  tcpVariants.insert (make_pair ("Vegas",         "ns3::TcpVegas"));
  tcpVariants.insert (make_pair ("Scalable",      "ns3::TcpScalable"));
  tcpVariants.insert (make_pair ("Veno",          "ns3::TcpVeno"));
  tcpVariants.insert (make_pair ("Bic",           "ns3::TcpBic"));
  tcpVariants.insert (make_pair ("Westwood",      "ns3::TcpWestwood"));
  tcpVariants.insert (make_pair ("WestwoodPlus",  "ns3::TcpWestwoodPlus"));

  /* Command line argument parser setup. */
  CommandLine cmd;
  cmd.AddValue ("applicationType", "Type of the Tx Application: onoff or bulk", applicationType);
  cmd.AddValue ("packetSize", "Application packet size [bytes]", packetSize);
  cmd.AddValue ("dataRate", "Application data rate", appDataRate);
  cmd.AddValue ("maxPackets", "Maximum number of packets to send", maxPackets);
  cmd.AddValue ("tcpVariant", "Transport protocol to use: TcpHighSpeed, TcpVegas, TcpNewReno, TcpWestwood, TcpWestwoodPlus", tcpVariant);
  cmd.AddValue ("socketType", "Socket type (default: ns3::UdpSocketFactory)", socketType);
  cmd.AddValue ("bufferSize", "TCP Buffer Size (Send/Receive) [bytes]", bufferSize);
  cmd.AddValue ("msduAggregation", "The maximum aggregation size for A-MSDU [bytes]", msduAggregationSize);
  cmd.AddValue ("mpduAggregation", "The maximum aggregation size for A-MPDU [bytes]", mpduAggregationSize);
  cmd.AddValue ("queueSize", "The maximum size of the Wifi MAC Queue [packets]", queueSize);
  cmd.AddValue ("frameCapture", "Use a frame capture model", frameCapture);
  cmd.AddValue ("frameCaptureMargin", "Frame capture model margin [dB]", frameCaptureMargin);
  cmd.AddValue ("phyMode", "802.11ad PHY Mode", phyMode);
  cmd.AddValue ("verbose", "turn on all WifiNetDevice log components", verbose);
  cmd.AddValue ("simulationTime", "Simulation time [s]", simulationTime);
  cmd.AddValue ("reportDataSnr", "Report SNR for data packets = True or for BF Control Packets = False", reportDataSnr);
  cmd.AddValue ("snapShotLength", "The maximum PCAP Snapshot Length", snapShotLength);
  cmd.AddValue ("qdChannelFolder", "The name of the folder containing the QD-Channel files", qdChannelFolder);
  cmd.AddValue ("numSTAs", "The number of DMG STA", numSTAs);
  cmd.AddValue ("pcap", "Enable PCAP Tracing", pcapTracing);
  cmd.AddValue ("scheduler", "The type of scheduler to use in the simulation", schedulerType);
  cmd.AddValue ("csv", "Enable CSV output instead of plain text. This mode will suppress all the messages related statistics and events.", csv);
  cmd.AddValue ("logComponentsStr", "Components to be logged from tLogStart to tLogEnd separated by ':'", logComponentsStr);
  cmd.AddValue ("tLogStart", "Log start [s]", tLogStart);
  cmd.AddValue ("tLogEnd", "Log end [s]", tLogEnd);
  cmd.Parse (argc, argv);

  /* Global params: no fragmentation, no RTS/CTS, fixed rate for all packets */
  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("999999"));
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("999999"));
  Config::SetDefault ("ns3::QueueBase::MaxPackets", UintegerValue (queueSize));

  /* Enable Log of specific components from tLogStart to tLogEnd */  
  vector<string> logComponents = SplitString (logComponentsStr, ':');
  EnableMyTraces (logComponents, Seconds (tLogStart), Seconds (tLogEnd));

  /*** Configure TCP Options ***/
  map<string, string>::const_iterator iter = tcpVariants.find (tcpVariant);
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

  /**** Set up Channel ****/
  Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel> ();
  Ptr<QdPropagationDelay> propagationDelayRayTracing = CreateObject<QdPropagationDelay> ();
  lossModelRaytracing = CreateObject<QdPropagationLossModel> ();
  lossModelRaytracing->SetAttribute ("QDModelFolder", StringValue ("DmgFiles/QdChannel/" + qdChannelFolder + "/"));
  propagationDelayRayTracing->SetAttribute ("QDModelFolder", StringValue ("DmgFiles/QdChannel/" + qdChannelFolder + "/"));
  spectrumChannel->AddSpectrumPropagationLossModel (lossModelRaytracing);
  spectrumChannel->SetPropagationDelayModel (propagationDelayRayTracing);

  /**** Setup physical layer ****/
  SpectrumDmgWifiPhyHelper spectrumWifiPhyHelper = SpectrumDmgWifiPhyHelper::Default ();
  spectrumWifiPhyHelper.SetChannel (spectrumChannel);
  /* All nodes transmit at 10 dBm == 10 mW, no adaptation */
  spectrumWifiPhyHelper.Set ("TxPowerStart", DoubleValue (10.0));
  spectrumWifiPhyHelper.Set ("TxPowerEnd", DoubleValue (10.0));
  spectrumWifiPhyHelper.Set ("TxPowerLevels", UintegerValue (1));

  if (frameCapture)
    {
      /* Frame Capture Model */
      spectrumWifiPhyHelper.Set ("FrameCaptureModel", StringValue ("ns3::SimpleFrameCaptureModel"));
      Config::SetDefault ("ns3::SimpleFrameCaptureModel::Margin", DoubleValue (frameCaptureMargin));
    }
  /* Set operating channel */
  spectrumWifiPhyHelper.Set ("ChannelNumber", UintegerValue (2));
  /* Set error model */
  spectrumWifiPhyHelper.SetErrorRateModel ("ns3::DmgErrorModel",
                                           "FileName", StringValue ("DmgFiles/ErrorModel/LookupTable_1458.txt"));
  /* Sensitivity model includes implementation loss and noise figure */
  spectrumWifiPhyHelper.Set ("CcaMode1Threshold", DoubleValue (-79));
  spectrumWifiPhyHelper.Set ("EnergyDetectionThreshold", DoubleValue (-79 + 3));

  /* Create 1 DMG PCP/AP */
  NodeContainer apWifiNode;
  apWifiNode.Create (1);
  /* Create numSTAs DMG STAs */
  NodeContainer staWifiNodes;
  staWifiNodes.Create (numSTAs);

  /**** WifiHelper is a meta-helper: it helps to create helpers ****/
  DmgWifiHelper wifiHelper;

  /* Set default algorithm for all nodes to be constant rate */
  wifiHelper.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "ControlMode", StringValue (phyMode),
                                      "DataMode", StringValue (phyMode));

  /* Add a DMG upper mac */
  DmgWifiMacHelper wifiMacHelper = DmgWifiMacHelper::Default ();

  Ssid ssid = Ssid ("SchedulerScenario");
  wifiMacHelper.SetType ("ns3::DmgApWifiMac",
                         "Ssid", SsidValue (ssid),
                         "BE_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                         "BE_MaxAmsduSize", UintegerValue (msduAggregationSize),
                         "BK_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                         "BK_MaxAmsduSize", UintegerValue (msduAggregationSize),
                         "VI_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                         "VI_MaxAmsduSize", UintegerValue (msduAggregationSize),
                         "VO_MaxAmpduSize", UintegerValue (mpduAggregationSize),
                         "VO_MaxAmsduSize", UintegerValue (msduAggregationSize));

  wifiMacHelper.SetAttribute ("SSSlotsPerABFT", UintegerValue (8), "SSFramesPerSlot", UintegerValue (13),
                              "BeaconInterval", TimeValue (MicroSeconds (102400)),
                              "ATIPresent", BooleanValue (false));
  
  /* Set Parametric Codebook for the DMG AP */
  wifiHelper.SetCodebook ("ns3::CodebookParametric",
                          "FileName", StringValue ("DmgFiles/Codebook/CODEBOOK_URA_AP_28x.txt"));

  /* Set the Scheduler for the DMG AP */
  wifiHelper.SetDmgScheduler (schedulerType);

  /* Create Wifi Network Devices (WifiNetDevice) */
  NetDeviceContainer apDevice;
  apDevice = wifiHelper.Install (spectrumWifiPhyHelper, wifiMacHelper, apWifiNode);

  wifiMacHelper.SetType ("ns3::DmgStaWifiMac",
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
  wifiHelper.SetCodebook ("ns3::CodebookParametric",
                          "FileName", StringValue ("DmgFiles/Codebook/CODEBOOK_URA_STA_28x.txt"));

  NetDeviceContainer staDevices;
  staDevices = wifiHelper.Install (spectrumWifiPhyHelper, wifiMacHelper, staWifiNodes);

  /* MAP MAC Addresses to NodeIDs */
  NetDeviceContainer devices;
  Ptr<WifiNetDevice> netDevice;
  devices.Add (apDevice);
  devices.Add (staDevices);
  for (uint32_t i = 0; i < devices.GetN (); i++)
    {
      netDevice = StaticCast<WifiNetDevice> (devices.Get (i));
      mac2IdMap[netDevice->GetMac ()->GetAddress ()] = netDevice->GetNode ()->GetId ();
      NS_LOG_UNCOND ("macAddress=" << netDevice->GetMac ()->GetAddress () << ", nodeId=" << netDevice->GetNode ()->GetId ());
    }

  /* Setting mobility model for AP */
  MobilityHelper mobilityAp;
  mobilityAp.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobilityAp.Install (apWifiNode);

  /* Setting mobility model for STA */
  MobilityHelper mobilitySta;
  mobilitySta.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobilitySta.Install (staWifiNodes);

  /* Internet stack*/
  InternetStackHelper stack;
  stack.Install (apWifiNode);
  stack.Install (staWifiNodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterface;
  apInterface = address.Assign (apDevice);
  Ipv4InterfaceContainer staInterfaces;
  staInterfaces = address.Assign (staDevices);

  /* We do not want any ARP packets */
  PopulateArpCache ();

  /** Install Applications **/
  vector<string> appDataRates = SplitString (appDataRateStr, ':');
  for (uint32_t i = 0; i < staWifiNodes.GetN (); i++)
    {
      string dataRate = appDataRate;
      if (appDataRates.size () != 0)
      {
        dataRate = appDataRates.at (i);
      }
      NS_LOG_UNCOND ("STA nodeId=" << staWifiNodes.Get (i)->GetId ());
      communicationPairList[staWifiNodes.Get (i)] = InstallApplications (staWifiNodes.Get (i), apWifiNode.Get (0),
                                                                         apInterface.GetAddress (0), dataRate, i);
    }

  /* Print Traces */
  if (pcapTracing)
    {
      spectrumWifiPhyHelper.SetPcapDataLinkType (SpectrumWifiPhyHelper::DLT_IEEE802_11_RADIO);
      spectrumWifiPhyHelper.EnablePcap ("Traces/AccessPoint", apDevice, false);
      spectrumWifiPhyHelper.EnablePcap ("Traces/STA", staDevices, false);
    }

  /* Callback for DMG STAs SLS */
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> outputSlsPhase = ascii.CreateFileStream ("slsResults.csv");
  *outputSlsPhase->GetStream () << "SRC_ID,DST_ID,TRACE_IDX,SECTOR_ID,ANTENNA_ID,ROLE,BSS_ID,Timestamp" << endl;

  /* Get SNR Traces */
  Ptr<OutputStreamWrapper> snrStream = ascii.CreateFileStream ("snrValues.csv");
  *snrStream->GetStream () << "TIME,SRC,DST,SNR" << endl;

  Ptr<WifiNetDevice> wifiNetDevice;
  Ptr<DmgStaWifiMac> staWifiMac;
  Ptr<WifiRemoteStationManager> remoteStationManager;

  /* Connect DMG STA traces */
  for (uint32_t i = 0; i < staDevices.GetN (); i++)
    {
      wifiNetDevice = StaticCast<WifiNetDevice> (staDevices.Get (i));
      staWifiMac = StaticCast<DmgStaWifiMac> (wifiNetDevice->GetMac ());
      remoteStationManager = wifiNetDevice->GetRemoteStationManager ();
      remoteStationManager->TraceConnectWithoutContext ("MacRxOK", MakeBoundCallback (&MacRxOk, staWifiMac, snrStream));
      staWifiMac->TraceConnectWithoutContext ("Assoc", MakeBoundCallback (&StationAssociated, staWifiNodes.Get (i), staWifiMac));
      staWifiMac->TraceConnectWithoutContext ("DeAssoc", MakeBoundCallback (&StationDeAssociated, staWifiNodes.Get (i), staWifiMac));
      staWifiMac->TraceConnectWithoutContext ("ADDTSResponse", MakeBoundCallback (&ADDTSResponseReceived, staWifiNodes.Get (i)));

      Ptr<Parameters> parameters = Create<Parameters> ();
      parameters->srcNodeID = wifiNetDevice->GetNode ()->GetId ();
      parameters->wifiMac = staWifiMac;
      staWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, outputSlsPhase, parameters));
      staWifiMac->TraceConnectWithoutContext ("DTIStarted", MakeBoundCallback (&DataTransmissionIntervalStarted, staWifiMac));
      biCounter[staWifiMac->GetAddress ()] = 0;
    }

  /* Connect DMG PCP/AP traces */
  wifiNetDevice = StaticCast<WifiNetDevice> (apDevice.Get (0));
  apWifiMac = StaticCast<DmgApWifiMac> (wifiNetDevice->GetMac ());
  remoteStationManager = wifiNetDevice->GetRemoteStationManager ();
  Ptr<Parameters> parameters = Create<Parameters> ();
  parameters->srcNodeID = wifiNetDevice->GetNode ()->GetId ();
  parameters->wifiMac = apWifiMac;
  apWifiMac->TraceConnectWithoutContext ("SLSCompleted", MakeBoundCallback (&SLSCompleted, outputSlsPhase, parameters));
  remoteStationManager->TraceConnectWithoutContext ("MacRxOK", MakeBoundCallback (&MacRxOk, apWifiMac, snrStream));

  /* Install FlowMonitor on all nodes */
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  /* Print Output */
  if (!csv)
    {
      cout << "Application Layer Throughput per Communicating Pair [Mbps]" << endl;
      cout << left << setw (12) << "Time [s]";
      string columnName;
      for (uint8_t i = 0; i < communicationPairList.size (); i++)
        {
          columnName = "Pair (" + std::to_string (i + 1) + ")";
          cout << left << setw (12) << columnName;
        }
      cout << left << setw (12) << "Total" << endl;
    }

  /* Schedule Throughput Calulcations */
  Simulator::Schedule (Seconds (0.1), &CalculateThroughput);

  Simulator::Stop (Seconds (simulationTime + 0.101));
  Simulator::Run ();
  Simulator::Destroy ();

  if (!csv)
    {
      /* Print per flow statistics */
      monitor->CheckForLostPackets ();
      Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
      FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
      for (map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
        {
          Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
          cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << endl;
          cout << "  Tx Packets: " << i->second.txPackets << endl;
          cout << "  Tx Bytes:   " << i->second.txBytes << endl;
          cout << "  Rx Packets: " << i->second.rxPackets << endl;
          cout << "  Rx Bytes:   " << i->second.rxBytes << endl;
        }

      /* Print Application Layer Results Summary */
      cout << "\nApplication Layer Statistics:" << endl;
      Ptr<OnOffApplication> onoff;
      Ptr<BulkSendApplication> bulk;
      uint16_t communicationLinks = 1;
      for (CommunicationPairListCI it = communicationPairList.begin (); it != communicationPairList.end (); ++it)
        {
          cout << "Communication Link (" << communicationLinks << ") Statistics:" << endl;
          if (applicationType == "onoff")
            {
              onoff = StaticCast<OnOffApplication> (it->second.srcApp);
              cout << "  Tx Packets: " << onoff->GetTotalTxPackets () << endl;
              cout << "  Tx Bytes:   " << onoff->GetTotalTxBytes () << endl;
            }
          else
            {
              bulk = StaticCast<BulkSendApplication> (it->second.srcApp);
              cout << "  Tx Packets: " << bulk->GetTotalTxPackets () << endl;
              cout << "  Tx Bytes:   " << bulk->GetTotalTxBytes () << endl;
            }
          Ptr<PacketSink> packetSink = StaticCast<PacketSink> (it->second.packetSink);
          cout << "  Rx Packets: " << packetSink->GetTotalReceivedPackets () << endl;
          cout << "  Rx Bytes:   " << packetSink->GetTotalRx () << endl;
          cout << "  Throughput: " << packetSink->GetTotalRx () * 8.0 / ((simulationTime - it->second.startTime.GetSeconds ()) * 1e6)
                    << " Mbps" << endl;
          communicationLinks++;
        }
    }

  return 0;
}