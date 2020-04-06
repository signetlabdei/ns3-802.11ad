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

#ifndef BASIC_DMG_WIFI_SCHEDULER_H
#define BASIC_DMG_WIFI_SCHEDULER_H

#include "dmg-wifi-scheduler.h"

namespace ns3 {
/**
 * \brief Basic scheduling features for IEEE 802.11ad
 *
 * This class provides the implementation of a basic set of scheduling features 
 * for IEEE 802.11ad. In particular, this class develops the admission and control
 * policy in the case of new ADDTS requests or modification ADDTS requests received.
 * The presence of a minimum broadcast CBAP time is considered when evaluating ADDTS requests.
 * The remaining DTI time is allocated as broadcast CBAP. 
 */
class BasicDmgWifiScheduler : public DmgWifiScheduler
{
public:
  static TypeId GetTypeId (void);

  BasicDmgWifiScheduler ();
  virtual ~BasicDmgWifiScheduler ();

protected:
  virtual void DoDispose (void);
  /**
   * \param minAllocation The minimum acceptable allocation in us for each allocation period.
   * \param maxAllocation The desired allocation in us for each allocation period.
   * \return The allocation duration for the allocation period.
   */
  virtual uint32_t GetAllocationDuration (uint32_t minAllocation, uint32_t maxAllocation);
  /**
   * Implement the policy that accept, reject a new ADDTS request.
   * \param sourceAid The AID of the requesting STA.
   * \param dmgTspec The DMG Tspec element of the ADDTS request.
   * \param info The DMG Allocation Info element of the request.
   * \return The Status Code to be included in the ADDTS response.
   */
  virtual StatusCode AddNewAllocation (uint8_t sourceAid, DmgTspecElement &dmgTspec, DmgAllocationInfo &info);
  /**
   * Implement the policy that accept, reject a modification request.
   * \param sourceAid The AID of the requesting STA.
   * \param dmgTspec The DMG Tspec element of the ADDTS request.
   * \param info The DMG Allocation Info element of the request.
   * \return The Status Code to be included in the ADDTS response.
   */
  virtual StatusCode ModifyExistingAllocation (uint8_t sourceAid, DmgTspecElement &dmgTspec, DmgAllocationInfo &info);
  /**
   * Adjust the existing allocations when an allocation is removed or modified.
   * \param iter The iterator pointing to the next element in the addtsAllocationList.
   * \param duration The duration of the time to manage.
   * \param isToAdd Whether the duration is to be added or subtracted.
   */
  virtual void AdjustExistingAllocations (AllocationFieldListI iter, uint32_t duration, bool isToAdd);
  /**
   * Update start time and remaining DTI time for the next request to be evaluated.
   */
  virtual void UpdateStartAndRemainingTime (void);
  /**
   * Add broadcast CBAP allocations in the DTI.
   */
  virtual void AddBroadcastCbapAllocations (void);

private:

  uint32_t m_minBroadcastCbapDuration;          //!< The minimum duration of a broadcast CBAP to be present in the DTI.
  uint32_t m_interAllocationDistance;           //!< The distance between two allocations to be used as broadcast CBAP.

};

} // namespace ns3

#endif /* DMG_WIFI_SCHEDULER_H */