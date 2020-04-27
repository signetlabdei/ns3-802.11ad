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
 *
 */

#include <algorithm>    // std::sort
#include <ns3/assert.h>
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <ns3/pointer.h>
#include <ns3/boolean.h>

#include "periodic-dmg-wifi-scheduler.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PeriodicDmgWifiScheduler");

NS_OBJECT_ENSURE_REGISTERED (PeriodicDmgWifiScheduler);

TypeId
PeriodicDmgWifiScheduler::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PeriodicDmgWifiScheduler")
      .SetParent<DmgWifiScheduler> ()
      .SetGroupName ("Wifi")
      .AddConstructor<PeriodicDmgWifiScheduler> ()

      .AddAttribute ("MinBroadcastCbapDuration", "The minimum duration in microseconds of a broadcast CBAP in the DTI",
                     UintegerValue (4096),
                     MakeUintegerAccessor (&PeriodicDmgWifiScheduler::m_minBroadcastCbapDuration),
                     MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("InterAllocationDistance", "The time distance in microseconds between two adjacent allocations "
                     "This distance will be allocated as broadcast CBAP",
                     UintegerValue (10),
                     MakeUintegerAccessor (&PeriodicDmgWifiScheduler::m_interAllocationDistance),
                     MakeUintegerChecker<uint32_t> (10, 65535))
  ;
  return tid;
}

PeriodicDmgWifiScheduler::PeriodicDmgWifiScheduler ()
{
  NS_LOG_FUNCTION (this);
}

PeriodicDmgWifiScheduler::~PeriodicDmgWifiScheduler ()
{
  NS_LOG_FUNCTION (this);
}

void
PeriodicDmgWifiScheduler::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  DmgWifiScheduler::DoDispose ();
}

void
PeriodicDmgWifiScheduler::UpdateStartAndRemainingTime (void)
{
  NS_LOG_FUNCTION (this);
  if (m_addtsAllocationList.empty ())
    {
      /* No existing allocations */
      m_allocationStartTime = 0;
      m_remainingDtiTime = m_dtiDuration.GetMicroSeconds ();
      m_availableSlots.push_back(std::make_pair(0, m_dtiDuration.GetMicroSeconds ()));
    }
  else
    {
      //TODO
    }
}

void
PeriodicDmgWifiScheduler::AdjustExistingAllocations (AllocationFieldListI iter, uint32_t duration, bool isToAdd)
{
  NS_LOG_FUNCTION (this << duration << isToAdd);
}

uint32_t
PeriodicDmgWifiScheduler::GetAllocationDuration (uint32_t minAllocation, uint32_t maxAllocation)
{
  NS_LOG_FUNCTION (this << minAllocation << maxAllocation);
  return ((minAllocation + maxAllocation) / 2);
}

StatusCode
PeriodicDmgWifiScheduler::AddNewAllocation (uint8_t sourceAid, const DmgTspecElement &dmgTspec, const DmgAllocationInfo &info)
{
  NS_LOG_FUNCTION (this);

  StatusCode status;
  uint32_t allocDuration;
  if (info.GetAllocationFormat () == ISOCHRONOUS)
    {
      allocDuration = GetAllocationDuration (dmgTspec.GetMinimumAllocation (), dmgTspec.GetMaximumAllocation ());
    }
  else if (info.GetAllocationFormat () == ASYNCHRONOUS)
    {
      /* for asynchronous allocations, the Maximum Allocation field is reserved (IEEE 802.11ad 8.4.2.136) */
      allocDuration = dmgTspec.GetMinimumAllocation ();
    }
  else
    {
      NS_FATAL_ERROR ("Allocation Format not supported");
    }

  /* Implementation of an admission policy for newly received requests. */
  uint16_t allocPeriod = dmgTspec.GetAllocationPeriod ();
  if (allocPeriod != 0)
    {
      /* Proceed with the allocation of periodic SPs */
      bool isMultiple = dmgTspec.IsAllocationPeriodMultipleBI();
      if (isMultiple)
      {
        NS_FATAL_ERROR ("Multiple BI periodicity is not supported yet.");
      }

      uint32_t spInterval = uint32_t(m_biDuration.GetMicroSeconds()/allocPeriod);
      if (spInterval - allocDuration < m_minBroadcastCbapDuration)
      {
        NS_FATAL_ERROR("These settings cannot guarantee minimum CBAP duration.");
      }

      NS_LOG_DEBUG("Allocation Period " << uint32_t(allocPeriod) << " AllocDuration " << allocDuration << " Multiple " << isMultiple);
      NS_LOG_DEBUG("Schedule one SP every " << spInterval);

      // ASSUMPTION : periodic requests are always the first to arrive

      std::pair<uint32_t, uint32_t> timeChunk;

      timeChunk = m_availableSlots.back();
      uint32_t startPeriodicAllocation = timeChunk.first;
      uint32_t endAlloc = timeChunk.first;

      while ((startPeriodicAllocation < timeChunk.second) && (startPeriodicAllocation+allocDuration <= timeChunk.second))
      {
        NS_LOG_DEBUG("Reserve from " << startPeriodicAllocation << " for " << allocDuration << " timeChunk.second  " << timeChunk.second );
        endAlloc = AllocateSingleContiguousBlock (info.GetAllocationID (), info.GetAllocationType (), info.IsPseudoStatic (),
                                                              sourceAid, info.GetDestinationAid (), startPeriodicAllocation, allocDuration);
        m_remainingDtiTime -= (allocDuration + m_guardTime);                                                      
        UpdateAvailableSlots(startPeriodicAllocation, endAlloc);
        startPeriodicAllocation += spInterval;
      }
      status.SetSuccess ();
    }
  else
    {
      
      if(m_availableSlots.begin() == m_availableSlots.end())
      {
        NS_LOG_DEBUG("There are no available slots.");
        status.SetFailure();
      }
      
      uint32_t endAlloc;
      uint32_t slotDur;
      for (auto it = m_availableSlots.begin() ; it != m_availableSlots.end(); ++it)
      {
        slotDur = it->second - it->first;
        
        if(slotDur > allocDuration)
        {
          
          endAlloc = AllocateSingleContiguousBlock (info.GetAllocationID (), info.GetAllocationType (), info.IsPseudoStatic (),
                                                                sourceAid, info.GetDestinationAid (), it->first, allocDuration);
          m_remainingDtiTime -= (allocDuration + m_guardTime);                                                      
          UpdateAvailableSlots(it->first, endAlloc);
          status.SetSuccess();
          break;                                                    
        }
        else
        {
          status.SetFailure(); 
        }
      }
      
    }

  return status;
}

void
PeriodicDmgWifiScheduler::UpdateAvailableSlots(uint32_t startAllocation, uint32_t endAlloc)
{
    NS_LOG_FUNCTION(this);

    std::vector<std::pair<uint32_t, uint32_t>> newDTI;

    for (auto it = m_availableSlots.begin() ; it != m_availableSlots.end(); ++it)
    {

      if (it->first > endAlloc || it->second < startAllocation)
      {
        newDTI.push_back(*it);
        continue;
      }

      if(it->first == startAllocation)
      {
        newDTI.push_back(std::make_pair(endAlloc, it->second));
      }
      else if (it->first < startAllocation && it->second > endAlloc)
      {
        newDTI.push_back(std::make_pair(it->first, startAllocation));
        newDTI.push_back(std::make_pair(endAlloc, it->second));
      }
    }

    for (auto it = newDTI.begin() ; it != newDTI.end(); ++it)
    {
      NS_LOG_DEBUG("Available slot from " << it->first << " to " << it->second);
    }

    m_availableSlots = newDTI;

}

StatusCode
PeriodicDmgWifiScheduler::ModifyExistingAllocation (uint8_t sourceAid, const DmgTspecElement &dmgTspec, const DmgAllocationInfo &info)
{
  NS_LOG_FUNCTION (this);

  StatusCode status;

  return status;
}

void
PeriodicDmgWifiScheduler::AddBroadcastCbapAllocations (void)
{
  NS_LOG_FUNCTION (this);
  /* Addts allocation list is copied to the allocation list */
  
  m_allocationList = m_addtsAllocationList;
  sort(m_allocationList.begin(), 
        m_allocationList.end(), 
        [](const AllocationField& lhs, const AllocationField& rhs){
            return lhs.GetAllocationStart() < rhs.GetAllocationStart();});
  
  AllocationFieldList broadcastCbapList;

  // fill all the remaining available slots with broadcast CBAPs
  
  for(auto itAll = m_allocationList.begin(); itAll != m_allocationList.end(); ++itAll)
  {
    auto itNextAll = itAll + 1;
    uint32_t start = itAll->GetAllocationStart ();
    uint32_t end =  start + itAll->GetAllocationBlockDuration () + m_guardTime;

    NS_LOG_DEBUG("Allocation start: " << start << " end: " << end);

    if(itNextAll != m_allocationList.end())
    {
      if (end < itNextAll->GetAllocationStart ())
      {
        broadcastCbapList = GetBroadcastCbapAllocation (true, end, itNextAll->GetAllocationStart() - end);
        m_remainingDtiTime -= itNextAll->GetAllocationStart() - end;
        itAll = m_allocationList.insert (itNextAll, broadcastCbapList.begin (), broadcastCbapList.end ());
        itAll += broadcastCbapList.size () - 1;

        NS_LOG_DEBUG("Added broadcast CBAPs list of size: " << broadcastCbapList.size () << " for a total duration of " << itNextAll->GetAllocationStart() - end);
      }
    }
    else 
    {
      broadcastCbapList = GetBroadcastCbapAllocation (true, end, m_remainingDtiTime);
      m_remainingDtiTime = 0;
      itAll = m_allocationList.insert (itNextAll, broadcastCbapList.begin (), broadcastCbapList.end ());
      itAll += broadcastCbapList.size () - 1;
    }
  }

}

} // namespace ns3
