/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
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
 * Based on
 *      NS-2 AODV model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 *
 *      AODV-UU implementation by Erik Nordström of Uppsala University
 *      http://core.it.uu.se/core/index.php/AODV-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */
#include "aodv-id-cache.h"

#include <algorithm>
#include <limits>

namespace ns3
{
namespace aodv
{
bool
IdCache::IsDuplicate(Ipv4Address addr, uint32_t id)
{
	return IsDuplicate(addr, id, std::numeric_limits<uint32_t>::max());
}

bool
IdCache::IsDuplicate(Ipv4Address addr, uint32_t id, uint32_t metric, bool unlimited)
{
	Purge();
	for (std::vector<UniqueId>::iterator i = m_idCache.begin(); i != m_idCache.end(); ++i)
	{
		if (i->m_context == addr && i->m_id == id)
		{
			i->m_expire = m_lifetime + Simulator::Now();
			if (metric < i->m_metric)
			{
				// Allow re-forward only up to MaxReforwardCount times
				// (destination nodes pass unlimited=true to bypass this limit)
				if (!unlimited && i->m_reforwardCount >= MaxReforwardCount)
				{
					return true;
				}
				i->m_metric = metric;
				if (!unlimited)
					i->m_reforwardCount++;
				return false;
			}
			return true;
		}
	}
	struct UniqueId uniqueId = {addr, id, metric, 0, m_lifetime + Simulator::Now()};
	m_idCache.push_back(uniqueId);
	return false;
}

void
IdCache::Purge()
{
	m_idCache.erase(remove_if(m_idCache.begin(), m_idCache.end(), IsExpired()), m_idCache.end());
}

uint32_t
IdCache::GetSize()
{
	Purge();
	return m_idCache.size();
}

} // namespace aodv
} // namespace ns3
