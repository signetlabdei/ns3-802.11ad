/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2020, University of Padova, Department of Information
 * Engineering, SIGNET Lab.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Salman Mohebi <s.mohebi22@gmail.com>
 *
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("EvaluateGamingTraffic");

/** Simulation Arguments **/
bool csv = false;  /* Enable CSV output. */
Time computeThroughputPeriodicity = MilliSeconds (1000);  /* Period in which throughput calculated */

double
GetThroughput (uint32_t bytes, Time duration)
{
  return bytes * 8.0 / duration.GetSeconds () / 1e6; /* Throughput in Mbps */
}

void
GeneratedPacketsStats (Ptr<OutputStreamWrapper> stream, Time& lastPacketTime, Ptr<const Packet> packet)
{
  Time currentPacketTime = Simulator::Now ();
  if (lastPacketTime.IsStrictlyNegative ())
    {
      lastPacketTime = currentPacketTime;
      return;
    }

  Time packetsInterArrivalTime = currentPacketTime - lastPacketTime;
  lastPacketTime = currentPacketTime;
  *stream->GetStream () << packet->GetSize ()  << ","
                        << packetsInterArrivalTime.GetSeconds () << std::endl;

}

void
CalculateThroughput (Ptr<OutputStreamWrapper> stream, Ptr<GamingStreamingServer> destination, uint64_t lastTotalRx)
{
  destination->GetTotalReceivedPackets ();
  Time now = Simulator::Now ();  /* Return the simulator's virtual time. */
  double cur = GetThroughput (destination->GetTotalReceivedBytes () - lastTotalRx , computeThroughputPeriodicity);
  *stream->GetStream () << now.GetSeconds ()  << "," << cur << std::endl;
  lastTotalRx = destination->GetTotalReceivedBytes ();
  Simulator::Schedule (computeThroughputPeriodicity, &CalculateThroughput, stream, destination, lastTotalRx);
}

int
main (int argc, char *argv[])
{
  Ptr<GamingStreamingServer> gamingServer;     /* Pointer to the gaming server application */
  uint64_t serverLastTotalRx = 0;              /* The value of the last total received bytes */
  uint64_t clientLastTotalRx = 0;              /* The value of the last total received bytes */
  Time serverLastPacketTime = Seconds (-1.0);  /* Time of last packet generated by server */
  Time clientLastPacketTime = Seconds (-1.0);  /* Time of last packet generated by client */
  std::string gamingServerId;                  /* TypeId of the gaming server */
  std::string gamingClientId;                  /* TypeId of the gaming server */

  bool summary = true;                      /* Print application layer traffic summary */
  double serverBitrate = 0;                 /* Gaming server data rate (in MBps) */
  double simulationTime = 10.0;             /* Simulation time in seconds */
  std::string gamingType = "CrazyTaxi";
  

  CommandLine cmd;
  cmd.AddValue ("summary", "Print summary of application layer traffic", summary);
  cmd.AddValue ("serverBitrate", "Gaming server data rate (in MBps), 0.0 to keep the default bitrate", serverBitrate );
  cmd.AddValue ("time", "Simulation time (in Seconds)", simulationTime );
  cmd.AddValue ("throughput", "Period in which throughput calculated", computeThroughputPeriodicity );
  cmd.AddValue ("game", "The gaming server type [\"CrazyTaxi\", \"FourElements\"]", gamingType );
  cmd.AddValue ("csv", "Enable saving result in .csv file", csv );
  cmd.Parse (argc, argv);

  if (gamingType == "CrazyTaxi")
    {
      gamingServerId = "ns3::CrazyTaxiStreamingServer";
      gamingClientId = "ns3::CrazyTaxiStreamingClient";
    }
  else if (gamingType == "FourElements")
    {
      gamingServerId = "ns3::FourElementsStreamingServer";
      gamingClientId = "ns3::FourElementsStreamingClient";
    }
  else
    {
      NS_FATAL_ERROR ("Invalid game");
    }

  LogComponentEnable ("GamingStreamingServer", LOG_LEVEL_INFO);
  LogComponentEnable ("CrazyTaxiStreamingServer", LOG_LEVEL_INFO);
  LogComponentEnable ("CrazyTaxiStreamingClient", LOG_LEVEL_INFO);

  NodeContainer nodes;
  nodes.Create (2);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("500Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer devices;
  devices = pointToPoint.Install (nodes);

  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");

  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  /* Gaming streaming server*/
  GamingStreamingServerHelper serverStreamingHelper (gamingServerId, interfaces.GetAddress (0), 9);
  serverStreamingHelper.SetAttribute ("BitRate", DoubleValue (serverBitrate));
  ApplicationContainer serverApps = serverStreamingHelper.Install (nodes.Get (1));
  gamingServer = StaticCast<GamingStreamingServer> (serverApps.Get (0));
  serverApps.Start (Seconds (0.01));
  serverApps.Stop (Seconds (simulationTime));

  /* Gaming streaming client*/
  GamingStreamingServerHelper clientStreamingHelper (gamingClientId, interfaces.GetAddress (1), 9);
  ApplicationContainer clientApps = clientStreamingHelper.Install (nodes.Get (0));
  gamingClient = StaticCast<GamingStreamingServer> (clientApps.Get (0));
  clientApps.Start (Seconds (0.01));
  clientApps.Stop (Seconds (simulationTime));


  if (csv)
    {     

      AsciiTraceHelper ascii;
      Ptr<OutputStreamWrapper> serverCdfResults = ascii.CreateFileStream ("serverCdfResults.csv");
      Ptr<OutputStreamWrapper> clientCdfResults = ascii.CreateFileStream ("clientCdfResults.csv");
      Ptr<OutputStreamWrapper> serverThroughputResults = ascii.CreateFileStream ("serverThroughputResults.csv");
      Ptr<OutputStreamWrapper> clientThroughputResults = ascii.CreateFileStream ("clientThroughputResults.csv");

      *serverCdfResults->GetStream () << "PKT_SIZE,IAT" << std::endl;
      *clientCdfResults->GetStream () << "PKT_SIZE,IAT" << std::endl;
      *serverThroughputResults->GetStream () << "TIME,THROUGHPUT" << std::endl;
      *clientThroughputResults->GetStream () << "TIME,THROUGHPUT" << std::endl;
      
      gamingServer->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&GeneratedPacketsStats, serverCdfResults, serverLastPacketTime));
      gamingClient->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&GeneratedPacketsStats, clientCdfResults, clientLastPacketTime));

      Simulator::Schedule (computeThroughputPeriodicity, &CalculateThroughput, serverThroughputResults, gamingClient, serverLastTotalRx);
      Simulator::Schedule (computeThroughputPeriodicity, &CalculateThroughput, clientThroughputResults, gamingServer, clientLastTotalRx);

    }

  Simulator::Stop (Seconds (simulationTime));
  Simulator::Run ();
  Simulator::Destroy ();

  if (summary)
    {
      NS_LOG_UNCOND ("\nApplication layer traffic summary: ");
      NS_LOG_UNCOND ("Total sent bytes by the server: " << gamingServer->GetTotalSentBytes () << 
                     " (" << gamingServer->GetTotalSentPackets () << " packets)");
      NS_LOG_UNCOND ("Total received bytes by the client: " << gamingClient->GetTotalReceivedBytes () << 
                     " ( " << gamingClient->GetTotalReceivedPackets () << " packets)");
      NS_LOG_UNCOND ("Total sent bytes by the client: " << gamingClient->GetTotalSentBytes () << 
                     " (" << gamingClient->GetTotalSentPackets () << " packets)");
      NS_LOG_UNCOND ("Total received bytes by the server: " << gamingServer->GetTotalReceivedBytes () << 
                     " ( " << gamingServer->GetTotalReceivedPackets () << " packets)");
      NS_LOG_UNCOND ("Number of failed packets: " << gamingServer->GetTotalFailedPackets ());
      NS_LOG_UNCOND ("Average server throughput:" <<  GetThroughput (gamingClient->GetTotalReceivedBytes () , Seconds (simulationTime)) << " Mbps");
      NS_LOG_UNCOND ("Average client throughput:" <<  GetThroughput (gamingServer->GetTotalReceivedBytes () , Seconds (simulationTime)) << " Mbps");
    }
  return 0;
}
