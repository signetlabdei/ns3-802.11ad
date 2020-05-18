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
    .AddConstructor<PeriodicDmgWifiScheduler> ();
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
  // for the periodic scheduler, m_allocationStartTime is useless, since the addition
  // of new SPs is consecutive 
  NS_LOG_FUNCTION (this);
  if (m_addtsAllocationList.empty ())
    {
      // no existing allocations
      m_remainingDtiTime = m_dtiDuration.GetMicroSeconds ();
      // clear the list of available slots: if no allocations have been scheduled, then the DTI is completely available
      m_availableSlots.clear ();
      m_availableSlots.push_back (std::make_pair (0, m_dtiDuration.GetMicroSeconds ()));
    }
  else
    {
      // if there are existing allocations, update DTI time just for consistency
      m_remainingDtiTime = 0;
      for (const auto & slot: m_availableSlots)
        {
          m_remainingDtiTime += (slot.second - slot.first);
        }
    }
}

void
PeriodicDmgWifiScheduler::AdjustExistingAllocations (AllocationFieldListI iter, uint32_t duration, bool isToAdd)
{
  NS_LOG_FUNCTION (this << duration << isToAdd);

  // This method is called upon a DelTsRequest or after the cleanup of 
  // non-pseudostatic allocations.
  // In this version of the periodic scheduler, existing allocations are not shifted 
  // to fill the created gaps but only the vector listing the available slots is updated.
  // For this reason, the current input parameters are useless.

  auto addtsListCopy = m_addtsAllocationList;

  // sort the copy to simplify the process of going through the allocation list
  sort (addtsListCopy.begin (),
        addtsListCopy.end (),
        [](const AllocationField& lhs, const AllocationField& rhs){
      return lhs.GetAllocationStart () < rhs.GetAllocationStart ();
    });

  uint32_t startAlloc, endAlloc;

  // clear m_availableSlots and refill it based on the updated m_addtsAllocationList
  m_availableSlots.clear ();
  m_availableSlots.push_back (std::make_pair (0, m_dtiDuration.GetMicroSeconds ()));
  // reset m_remainingDtiTime as well
  m_remainingDtiTime = m_dtiDuration.GetMicroSeconds ();

  for (const auto & allocation: addtsListCopy)
    {
      startAlloc = allocation.GetAllocationStart ();
      endAlloc = startAlloc + allocation.GetAllocationBlockDuration () + m_guardTime;
      // if the number of blocks allocated is > 1, the allocation is periodic 
      uint8_t blocks = allocation.GetNumberOfBlocks ();
      while (blocks > 0)
        {
          UpdateAvailableSlots (startAlloc, endAlloc);
          startAlloc += allocation.GetAllocationBlockPeriod ();
          endAlloc += allocation.GetAllocationBlockPeriod ();
          // AllocationBlockPeriod represents the time between the start of two consecutive time blocks belonging to the same allocation
          blocks -= 1;
        }
    }
}

uint32_t
PeriodicDmgWifiScheduler::GetAllocationDuration (uint32_t minAllocation, uint32_t maxAllocation)
{
  NS_LOG_FUNCTION (this << minAllocation << maxAllocation);
  return maxAllocation;
}

StatusCode
PeriodicDmgWifiScheduler::AddNewAllocation (uint8_t sourceAid, const DmgTspecElement &dmgTspec, const DmgAllocationInfo &info)
{
  NS_LOG_FUNCTION (this << +sourceAid);

  StatusCode status;
  uint32_t allocDuration;

  if (m_availableSlots.empty ())
    {
      NS_LOG_DEBUG ("There are no free available slots in the DTI.");
      status.SetFailure ();
      return status;
    }

  if (info.GetAllocationFormat () == ISOCHRONOUS)
    {
      allocDuration = GetAllocationDuration (dmgTspec.GetMinimumAllocation (), dmgTspec.GetMaximumAllocation ());
      if (allocDuration < dmgTspec.GetMinimumAllocation ())
        {
          NS_LOG_DEBUG ("Unable to guarantee minimum duration.");
          status.SetFailure ();
          return status;
        }
    }
  else if (info.GetAllocationFormat () == ASYNCHRONOUS)
    {
      // for asynchronous allocations, the Maximum Allocation field is reserved (IEEE 802.11ad 8.4.2.136)
      allocDuration = dmgTspec.GetMinimumAllocation ();
    }
  else
    {
      NS_FATAL_ERROR ("Allocation Format not supported");
    }

  uint16_t allocPeriod = dmgTspec.GetAllocationPeriod ();
  std::vector<uint32_t> blocks;
  uint32_t spInterval = 0;
  
  if (allocPeriod != 0)
    {
      NS_ABORT_MSG_IF (dmgTspec.IsAllocationPeriodMultipleBI (), "Multiple BI periodicity is not supported.");
      // spInterval is going to be passed to AddAllocationPeriod to specify the 
      // distance between consecutive periodic SPs
      spInterval = uint32_t (m_biDuration.GetMicroSeconds () / allocPeriod);

      NS_LOG_DEBUG ("Allocation Period " << allocPeriod 
      << " AllocDuration " << allocDuration 
      << " - Schedule one SP every " << spInterval);

      blocks = GetAvailableBlocks (allocDuration, spInterval, MAX_NUM_BLOCKS);
      
      if (blocks.size() <= 1)
        {
          // if we cannot guarantee AT LEAST TWO periodic SPs, the request is rejected
          status.SetFailure ();
          return status;
        }
    }
    else
    {
      blocks = GetAvailableBlocks (allocDuration, 0, 1);
      if (blocks.size() == 0)
        {
          // if we cannot guarantee the SP allocation, the request is rejected
          status.SetFailure ();
          return status;
        }
    }
    
  uint32_t startAlloc = blocks[0];
  uint32_t endAlloc;
  uint8_t counter = 0;
  
  while (counter < blocks.size())
    {
      NS_LOG_DEBUG ("Reserve from " << startAlloc << " for " << allocDuration);

      endAlloc = startAlloc + allocDuration + m_guardTime;
      UpdateAvailableSlots (startAlloc, endAlloc);

      startAlloc += spInterval;
      counter++;
    }

  AddAllocationPeriod (info.GetAllocationID (), info.GetAllocationType (), info.IsPseudoStatic (),
                       sourceAid, info.GetDestinationAid (),
                       blocks[0], allocDuration, spInterval, blocks.size());
  
  status.SetSuccess ();
  
  return status;
}

std::vector<uint32_t>
PeriodicDmgWifiScheduler::GetAvailableBlocks (uint32_t allocDuration, uint32_t spInterval, uint8_t maxBlocksNumber)
{
  NS_LOG_FUNCTION (this << allocDuration << spInterval);

  auto it = m_availableSlots.begin ();
  uint32_t startNextAlloc = it->first;
  uint32_t remainingSlotDuration = 0;
  std::vector<uint32_t> blocks;

  while (it != m_availableSlots.end ())
    {
      if (startNextAlloc < it->first)
        {
          // The next periodic SP should be positioned before the beginning of the 
          // next available slot: this translates in a broken periodicity, and the 
          // algorithm stops.
          break;
        }

      remainingSlotDuration = it->second - startNextAlloc;

      if (allocDuration + m_guardTime > remainingSlotDuration)
        {
          if (blocks.size() == 0)
            {
              // go on until eventually find the first available slot that fits this SP
              // note that this also cover the condition where no slot satisfies the requirement
              ++it;
              startNextAlloc = it->first;
              continue;
            }
          else
            {
              // if one or more periodic SPs already allocated, now the periodicity
              // is broken and the algorithm stops
              break;
            }
        }

      blocks.push_back(startNextAlloc);
      startNextAlloc += spInterval;
      
      if (blocks.size() == maxBlocksNumber)
        {
          // Number of blocks described by an octet: only up to 255 blocks
          break;
        }

      // if next allocation period exceeds the current available slot's boundaries
      // proceed to the next one
      if (startNextAlloc > it->second)
        {
          ++it;
        }
    }

  return blocks;
}

void
PeriodicDmgWifiScheduler::UpdateAvailableSlots (uint32_t startAlloc, uint32_t endAlloc)
{
  NS_LOG_FUNCTION (this << startAlloc << endAlloc);

  std::vector<std::pair<uint32_t, uint32_t> > newDti;

  for (const auto & slot: m_availableSlots)
    {
      if (slot.first > endAlloc || slot.second < startAlloc)
        {
          newDti.push_back (slot);
          continue;
        }

      if (slot.first == startAlloc)
        {
          newDti.push_back (std::make_pair (endAlloc, slot.second));
        }
      else if (slot.first < startAlloc && slot.second > endAlloc)
        {
          newDti.push_back (std::make_pair (slot.first, startAlloc));
          newDti.push_back (std::make_pair (endAlloc, slot.second));
        }
    }
  
  // update m_remainingDtiTime for consistency 
  m_remainingDtiTime -= (endAlloc - startAlloc);
  
  m_availableSlots = newDti;

  for (const auto & slot : m_availableSlots)
    {
      NS_LOG_DEBUG ("Available slot from " << slot.first << " to " << slot.second);
    }
}

void
PeriodicDmgWifiScheduler::UpdateAvailableSlots (uint32_t startAlloc, uint32_t endAlloc, uint32_t difference)
{
  NS_LOG_FUNCTION (this << startAlloc << endAlloc << difference);

  std::vector<std::pair<uint32_t, uint32_t> > newDti;

  // something has changed in the allocation list, need to change the list of 
  // available slots accordingly
  bool keepSearching = true;
  for (const auto & slot: m_availableSlots)
    {

      if (slot.first < startAlloc)
        {
          newDti.push_back (slot);
          continue;
        }

      if (endAlloc < slot.first)
        {
          // the following snippet ensures that the search is carried on until 
          // we find the empty gap created by the allocation time reduction
          // and we add it to the list of available slots 
          if (keepSearching)
            {
              if (difference == (slot.first - endAlloc))
                {
                  // this make sure that two adjacent available slots are merged 
                  newDti.push_back (std::make_pair (endAlloc, slot.second));
                  keepSearching = false;
                }
              else if (difference < (slot.first - endAlloc))
                {
                  // this condition is verified when there is one or more allocations 
                  // between the gap and the following available slot
                  newDti.push_back (std::make_pair (endAlloc, endAlloc + difference));
                  newDti.push_back (slot);
                  keepSearching = false;
                }
              NS_ASSERT_MSG (difference <= (slot.first - endAlloc), "Something broke in runtime, check the update of the available slots.");
            }
          else
            {
              newDti.push_back (slot);
            }
        }
    }
  
  m_remainingDtiTime += difference;
  m_availableSlots = newDti;

  for (const auto & slot : m_availableSlots)
    {
      NS_LOG_DEBUG ("Available slot from " << slot.first << " to " << slot.second);
    }

}

StatusCode
PeriodicDmgWifiScheduler::ModifyExistingAllocation (uint8_t sourceAid, const DmgTspecElement &dmgTspec, const DmgAllocationInfo &info)
{
  NS_LOG_FUNCTION (this << +sourceAid);

  StatusCode status;
  uint32_t newDuration;
  if (info.GetAllocationFormat () == ISOCHRONOUS)
    {
      newDuration = GetAllocationDuration (dmgTspec.GetMinimumAllocation (), dmgTspec.GetMaximumAllocation ());
    }
  else if (info.GetAllocationFormat () == ASYNCHRONOUS)
    {
      // for asynchronous allocations, the Maximum Allocation field is reserved (IEEE 802.11ad 8.4.2.136) 
      newDuration = dmgTspec.GetMinimumAllocation ();
    }
  else
    {
      NS_FATAL_ERROR ("Allocation Format not supported");
    }

  AllocationFieldListI allocation;
  // Retrieve the allocation for which a modification has been requested
  for (allocation = m_addtsAllocationList.begin (); allocation != m_addtsAllocationList.end ();)
    {
      if ((allocation->GetAllocationID () == info.GetAllocationID ())
          && (allocation->GetSourceAid () == sourceAid) 
          && (allocation->GetDestinationAid () == info.GetDestinationAid ()))
        {
          break;
        }
      else
        {
          ++allocation;
        }
    }

  NS_ABORT_MSG_IF (allocation == m_addtsAllocationList.end (), "Required allocation does not exist.");

  uint32_t currentDuration = allocation->GetAllocationBlockDuration ();
  NS_LOG_DEBUG ("current duration=" << currentDuration << ", new duration=" << newDuration);
  uint32_t timeDifference;
  if (newDuration > currentDuration)
    {
      NS_LOG_DEBUG ("The increase in slot duration is not supported by this version of PeriodicDmgWifiScheduler.");
      // The request cannot be accepted; maintaining old allocation duration
      // No need to update allocation start time and remaining DTI time 
      status.SetFailure ();
    }
  else
    {
      NS_LOG_DEBUG ("Reduction of the duration is always allowed. Proceed to update the available slots...");
      timeDifference = currentDuration - newDuration;
      allocation->SetAllocationBlockDuration (newDuration);
      status.SetSuccess ();

      uint32_t startAlloc = allocation->GetAllocationStart ();
      uint32_t endAlloc = allocation->GetAllocationStart () + allocation->GetAllocationBlockDuration () + m_guardTime;
      uint16_t allocPeriod = allocation->GetAllocationBlockPeriod ();
  
      // If the number of blocks is > 0 we have to update the available slots in the DTI accordingly.
      // TODO: update also the number of blocks if the new duration allows to 
      // add further blocks
      uint8_t blocks = allocation->GetNumberOfBlocks ();
      while (blocks > 0)
        {
          NS_LOG_DEBUG ("Modify SP Block at " << startAlloc << " till " << endAlloc);
          UpdateAvailableSlots (startAlloc, endAlloc, timeDifference);
          startAlloc += allocPeriod;
          endAlloc += allocPeriod;
          blocks -= 1;
        }
    }
  return status;
}

void
PeriodicDmgWifiScheduler::AddBroadcastCbapAllocations (void)
{
  NS_LOG_FUNCTION (this);

  // Addts allocation list is copied to the allocation list 

  m_allocationList = m_addtsAllocationList;
  AllocationFieldList broadcastCbapList;

  // fill all the remaining available slots with broadcast CBAPs

  for (const auto & slot : m_availableSlots)
    {
      broadcastCbapList = GetBroadcastCbapAllocation (true, slot.first, slot.second - slot.first);
      m_remainingDtiTime -= slot.second - slot.first;
      m_allocationList.insert (m_allocationList.begin (), broadcastCbapList.begin (), broadcastCbapList.end ());
      NS_LOG_DEBUG ("Added broadcast CBAPs list of size: " << broadcastCbapList.size () << " for a total duration of " << slot.second - slot.first);
    }

  sort (m_allocationList.begin (),
        m_allocationList.end (),
        [](const AllocationField& lhs, const AllocationField& rhs){
      return lhs.GetAllocationStart () < rhs.GetAllocationStart ();
    });

  for (const auto & alloc: m_allocationList)
    {
      NS_LOG_DEBUG ("Allocation element start at: " << alloc.GetAllocationStart () << " periodicity " << alloc.GetAllocationBlockPeriod () << " duration " << alloc.GetAllocationBlockDuration ());
    }

}

} // namespace ns3
