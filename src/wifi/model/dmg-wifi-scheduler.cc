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
 * Authors: Tommy Azzino <tommy.azzino@gmail.com>
 *
 */

#include <ns3/assert.h>
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <ns3/pointer.h>
#include <ns3/boolean.h>

#include "dmg-wifi-scheduler.h"
#include "dmg-ap-wifi-mac.h"
#include "wifi-utils.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DmgWifiScheduler");

NS_OBJECT_ENSURE_REGISTERED (DmgWifiScheduler);

TypeId
DmgWifiScheduler::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::DmgWifiScheduler")
                      .SetParent<Object> ()
                      .SetGroupName ("Wifi")
  ;
  return tid;
}

DmgWifiScheduler::DmgWifiScheduler ()
{
  NS_LOG_FUNCTION (this);
}

DmgWifiScheduler::~DmgWifiScheduler ()
{
  NS_LOG_FUNCTION (this);
}

void
DmgWifiScheduler::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  m_mac = 0;
  m_receiveAddtsRequests.clear ();
}

void 
DmgWifiScheduler::SetMac (Ptr<DmgApWifiMac> mac)
{
  NS_LOG_FUNCTION (this << mac);
  m_mac = mac;
}

void
DmgWifiScheduler::Initialize (void)
{
  NS_LOG_FUNCTION (this);
  DoInitialize ();
}

void
DmgWifiScheduler::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  bool isConnected;
  isConnected = m_mac->TraceConnectWithoutContext ("ADDTSReceived", MakeCallback (&DmgWifiScheduler::ReceiveAddtsRequest, this));
  isConnected = m_mac->TraceConnectWithoutContext ("BIStarted", MakeCallback (&DmgWifiScheduler::BeaconIntervalStarted, this));
  isConnected = m_mac->TraceConnectWithoutContext ("DELTSReceived", MakeCallback (&DmgWifiScheduler::ReceiveDeltsRequest, this));
  NS_ASSERT_MSG (isConnected, "Connection to Traces failed.");
}

AllocationFieldList
DmgWifiScheduler::GetAllocationList (void)
{
  return m_allocationList;
}

void
DmgWifiScheduler::SetAllocationList (AllocationFieldList allocationList)
{
  m_allocationList = allocationList;
}

void 
DmgWifiScheduler::BeaconIntervalStarted (Mac48Address address, Time biDuration, Time bhiDuration, Time atiDuration)
{
  NS_LOG_INFO ("Beacon Interval started at " << Simulator::Now ());
  m_biStartTime = Simulator::Now ();
  m_accessPeriod = CHANNEL_ACCESS_BHI;
  m_biDuration = biDuration;
  m_bhiDuration = bhiDuration;
  m_atiDuration = atiDuration;
  m_dtiDuration = m_biDuration - m_bhiDuration;
  if (m_atiDuration.IsStrictlyPositive ())
    {
      Simulator::Schedule (m_bhiDuration - m_atiDuration - m_mac->GetMbifs (), 
                           &DmgWifiScheduler::AnnouncementTransmissionIntervalStarted, this);
    }
  else
    {
      Simulator::Schedule (m_bhiDuration, &DmgWifiScheduler::DataTransferIntervalStarted, this);
    }
}

void
DmgWifiScheduler::AnnouncementTransmissionIntervalStarted (void)
{
  NS_LOG_INFO ("ATI started at " << Simulator::Now ());
  m_atiStartTime = Simulator::Now ();
  m_accessPeriod = CHANNEL_ACCESS_ATI;
  Simulator::Schedule (m_atiDuration, &DmgWifiScheduler::DataTransferIntervalStarted, this);
}

void 
DmgWifiScheduler::DataTransferIntervalStarted (void)
{
  NS_LOG_INFO ("DTI started at " << Simulator::Now ());
  m_dtiStartTime = Simulator::Now ();
  m_accessPeriod = CHANNEL_ACCESS_DTI;
  Simulator::Schedule (m_dtiDuration, &DmgWifiScheduler::BeaconIntervalEnded, this);
}

void
DmgWifiScheduler::BeaconIntervalEnded (void)
{
  NS_LOG_INFO ("Beacon Interval ended at " << Simulator::Now ());
  /* Cleanup non-static allocations. */
  CleanupAllocations ();
  /* Do something with the ADDTS requests received in the last DTI (if any) */
  if (!m_receiveAddtsRequests.empty ())
    {
      /* At least one ADDTS request has been received */
      ManageAddtsRequests (); 
    }
}

void
DmgWifiScheduler::ReceiveDeltsRequest (Mac48Address address, DmgAllocationInfo info)
{
  NS_LOG_DEBUG ("Receive DELTS request from " << address);
  uint8_t stationAid = m_mac->GetStationAid (address);
  /* Check whether this allocation has been previously allocated */
  AllocatedRequestMapI it = m_allocatedAddtsRequests.find (UniqueIdentifier (info.GetAllocationID (), 
                                                                             stationAid, info.GetDestinationAid ()));
  if (it != m_allocatedAddtsRequests.end ())
    {
      /* Delete allocation from m_allocatedAddtsRequests and m_allocationList */
      m_allocatedAddtsRequests.erase (it);
      AllocationField allocation;
      for (AllocationFieldListI iter = m_allocationList.begin (); iter != m_allocationList.end ();)
        {
          allocation = (*iter);
          if ((allocation.GetAllocationID () == info.GetAllocationID ()) &&
              (allocation.GetSourceAid () == stationAid) &&
              (allocation.GetDestinationAid () == info.GetDestinationAid ()))
            {
              iter = m_allocationList.erase (iter);
              break;
            }
          else
            {
              ++iter;
            }
        }
    }
  else
    {
      /* The allocation does not exist */
      NS_LOG_DEBUG ("Cannot find the allocation");
    }
}

void
DmgWifiScheduler::ReceiveAddtsRequest (Mac48Address address, DmgTspecElement element)
{
  NS_LOG_DEBUG ("Receive ADDTS request from " << address);
  /* Store the ADDTS request received in the current DTI */
  AddtsRequest request;
  request.sourceAid = m_mac->GetStationAid (address);
  request.dmgTspec = element;
  m_receiveAddtsRequests.push_back (request);

}

void
DmgWifiScheduler::ManageAddtsRequests (void)
{
  /* Manage the ADDTS requests received in the last DTI.
   * Implementation of admission policies for IEEE 802.11ad.
   * Channel access organization during the DTI.
   */
  NS_LOG_FUNCTION (this);

  AddtsRequest request;
  DmgTspecElement dmgTspec;
  /* Cycle over the list of received ADDTS requests; remainingDtiTime is updated each time an allocation is accepted.
   * Once all requests have been evaluated (accepted or rejected):
   * allocate remainingDtiTime (if > 0) as CBAP with destination & source AID to Broadcast */
  for (AddtsRequestListCI iter = m_receiveAddtsRequests.begin (); iter != m_receiveAddtsRequests.end (); iter++)
    {
      request = (*iter);
      dmgTspec = request.dmgTspec;

    }
}

uint32_t
DmgWifiScheduler::AllocateCbapPeriod (bool staticAllocation, uint32_t allocationStart, uint16_t blockDuration)
{
  NS_LOG_FUNCTION (this << staticAllocation << allocationStart << blockDuration);
  AllocateSingleContiguousBlock (0, CBAP_ALLOCATION, staticAllocation, AID_BROADCAST, AID_BROADCAST, allocationStart, blockDuration);
  return (allocationStart + blockDuration);
}

uint32_t
DmgWifiScheduler::AllocateSingleContiguousBlock (AllocationID allocationId, AllocationType allocationType, bool staticAllocation,
                                                 uint8_t sourceAid, uint8_t destAid, uint32_t allocationStart, uint16_t blockDuration)
{
  NS_LOG_FUNCTION (this);
  return (AddAllocationPeriod (allocationId, allocationType, staticAllocation, sourceAid, destAid,
                               allocationStart, blockDuration, 0, 1));
}

uint32_t
DmgWifiScheduler::AllocateMultipleContiguousBlocks (AllocationID allocationId, AllocationType allocationType, bool staticAllocation,
                                                    uint8_t sourceAid, uint8_t destAid, uint32_t allocationStart, uint16_t blockDuration, uint8_t blocks)
{
  NS_LOG_FUNCTION (this);
  AddAllocationPeriod (allocationId, allocationType, staticAllocation, sourceAid, destAid,
                       allocationStart, blockDuration, 0, blocks);
  return (allocationStart + blockDuration * blocks);
}

void
DmgWifiScheduler::AllocateDTIAsServicePeriod (AllocationID allocationId, uint8_t sourceAid, uint8_t destAid)
{
  NS_LOG_FUNCTION (this);
  uint16_t spDuration = floor (m_dtiDuration.GetMicroSeconds () / MAX_NUM_BLOCKS);
  AddAllocationPeriod (allocationId, SERVICE_PERIOD_ALLOCATION, true, sourceAid, destAid,
                       0, spDuration, 0, MAX_NUM_BLOCKS);
}

uint32_t
DmgWifiScheduler::AddAllocationPeriod (AllocationID allocationId, AllocationType allocationType, bool staticAllocation,
                                       uint8_t sourceAid, uint8_t destAid, uint32_t allocationStart, uint16_t blockDuration,
                                       uint16_t blockPeriod, uint8_t blocks)
{
  NS_LOG_FUNCTION (this << +allocationId << allocationType << staticAllocation << +sourceAid 
                   << +destAid << allocationStart << blockDuration << blockPeriod << +blocks);
  AllocationField field;
  /* Allocation Control Field */
  field.SetAllocationID (allocationId);
  field.SetAllocationType (allocationType);
  field.SetAsPseudoStatic (staticAllocation);
  /* Allocation Field */
  field.SetSourceAid (sourceAid);
  field.SetDestinationAid (destAid);
  field.SetAllocationStart (allocationStart);
  field.SetAllocationBlockDuration (blockDuration);
  field.SetAllocationBlockPeriod (blockPeriod);
  field.SetNumberOfBlocks (blocks);
  /**
   * When scheduling two adjacent SPs, the PCP/AP should allocate the SPs separated by at least
   * aDMGPPMinListeningTime if one or more of the source or destination DMG STAs participate in both SPs.
   */
  m_allocationList.push_back (field);

  return (allocationStart + blockDuration);
}

uint32_t
DmgWifiScheduler::AllocateBeamformingServicePeriod (uint8_t sourceAid, uint8_t destAid, uint32_t allocationStart, bool isTxss)
{
  return AllocateBeamformingServicePeriod (sourceAid, destAid, allocationStart, 2000, isTxss, isTxss);
}

uint32_t
DmgWifiScheduler::AllocateBeamformingServicePeriod (uint8_t sourceAid, uint8_t destAid, uint32_t allocationStart,
                                                    uint16_t allocationDuration, bool isInitiatorTxss, bool isResponderTxss)
{
  NS_LOG_FUNCTION (this << +sourceAid << +destAid << allocationStart << allocationDuration << isInitiatorTxss << isResponderTxss);
  AllocationField field;
  /* Allocation Control Field */
  field.SetAllocationType (SERVICE_PERIOD_ALLOCATION);
  field.SetAsPseudoStatic (false);
  /* Allocation Field */
  field.SetSourceAid (sourceAid);
  field.SetDestinationAid (destAid);
  field.SetAllocationStart (allocationStart);
  field.SetAllocationBlockDuration (allocationDuration);     // Microseconds
  field.SetNumberOfBlocks (1);

  BF_Control_Field bfField;
  bfField.SetBeamformTraining (true);
  bfField.SetAsInitiatorTxss (isInitiatorTxss);
  bfField.SetAsResponderTxss (isResponderTxss);

  field.SetBfControl (bfField);
  m_allocationList.push_back (field);

  return (allocationStart + allocationDuration + 1000); // 1000 = 1 us protection period
}

uint32_t
DmgWifiScheduler::GetAllocationListSize (void) const
{
  return m_allocationList.size ();
}

void
DmgWifiScheduler::CleanupAllocations (void)
{
  NS_LOG_FUNCTION (this);
  AllocationField allocation;
  for(AllocationFieldListI iter = m_allocationList.begin (); iter != m_allocationList.end ();)
    {
      allocation = (*iter);
      if (!allocation.IsPseudoStatic () && iter->IsAllocationAnnounced ())
        {
          iter = m_allocationList.erase (iter);
        }
      else
        {
          ++iter;
        }
    }
}

void
DmgWifiScheduler::ModifyAllocation (AllocationID allocationId, uint8_t sourceAid, uint8_t destAid, 
                                    uint32_t newStartTime, uint16_t newDuration)
{
  NS_LOG_FUNCTION (this << +allocationId << +sourceAid << +destAid << newStartTime << newDuration);
  for (AllocationFieldListI iter = m_allocationList.begin (); iter != m_allocationList.end (); iter++)
    {
      AllocationField field = (*iter);
      if ((field.GetAllocationID () == allocationId) &&
          (field.GetSourceAid () == sourceAid) && (field.GetDestinationAid () == destAid))
        {
          field.SetAllocationStart (newStartTime);
          field.SetAllocationBlockDuration (newDuration);
          break;
        }
    }
}

} // namespace ns3