/*
    Copyright 2013 Brain Research Institute, Melbourne, Australia

    Written by Robert Smith, 2015.

    This file is part of MRtrix.

    MRtrix is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MRtrix is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MRtrix.  If not, see <http://www.gnu.org/licenses/>.

 */



#include "dwi/tractography/connectome/exemplar.h"


// Fraction of the streamline length at each end that will be pulled toward the node centre-of-mass
// TODO Make this a fraction of length, rather than fraction of points?
#define EXEMPLAR_ENDPOINT_CONVERGE_FRACTION 0.25



namespace MR {
namespace DWI {
namespace Tractography {
namespace Connectome {



Exemplar& Exemplar::operator= (const Exemplar& that)
{
  Tractography::Streamline<float>(*this) = that;
  nodes = that.nodes;
  node_COMs = that.node_COMs;
  is_finalized = that.is_finalized;
  return *this;
}

void Exemplar::add (const Tractography::Connectome::Streamline& in)
{
  assert (!is_finalized);
  std::lock_guard<std::mutex> lock (mutex);
  // Determine whether or not this streamline is reversed w.r.t. the exemplar
  // Now the ordering of the nodes is retained; use those
  bool is_reversed = false;
  if (in.get_nodes() != nodes) {
    is_reversed = true;
    assert (in.get_nodes().first == nodes.second && in.get_nodes().second == nodes.first);
  }
  // Contribute this streamline toward the mean exemplar streamline
  for (uint32_t i = 0; i != size(); ++i) {
    float interp_pos = (in.size() - 1) * i / float(size());
    if (is_reversed)
      interp_pos = in.size() - 1 - interp_pos;
    const uint32_t lower = std::floor (interp_pos), upper (lower + 1);
    const float mu = interp_pos - lower;
    Point<float> pos;
    if (lower == in.size() - 1)
      pos = in.back();
    else
      pos = ((1.0f-mu) * in[lower]) + (mu * in[upper]);
    (*this)[i] += (pos * in.weight);
  }
  weight += in.weight;
}




void Exemplar::finalize (const float step_size)
{
  assert (!is_finalized);
  std::lock_guard<std::mutex> lock (mutex);

  if (!weight || is_diagonal()) {
    // No streamlines assigned, or a diagonal in the matrix; generate a straight line between the two nodes
    // FIXME Is there an error in the new tractography tool that is causing omission of the first track segment?
    clear();
    push_back (node_COMs.first);
    push_back (node_COMs.second);
    return;
  }

  const float multiplier = 1.0f / weight;
  for (std::vector< Point<float> >::iterator i = begin(); i != end(); ++i)
    *i *= multiplier;

  // Constrain endpoints to the node centres of mass
  uint32_t num_converging_points = EXEMPLAR_ENDPOINT_CONVERGE_FRACTION * size();
  for (uint32_t i = 0; i != num_converging_points; ++i) {
    const float mu = i / float(num_converging_points);
    (*this)[i] = (mu * (*this)[i]) + ((1.0f-mu) * node_COMs.first);
  }
  for (uint32_t i = size() - 1; i != size() - 1 - num_converging_points; --i) {
    const float mu = (size() - 1 - i) / float(num_converging_points);
    (*this)[i] = (mu * (*this)[i]) + ((1.0f-mu) * node_COMs.second);
  }

  // Resample to fixed step size
  // Start from the midpoint, resample backwards to the start of the exemplar,
  //   reverse the data, then do the second half of the exemplar
  int32_t index = (size() + 1) / 2;
  std::vector< Point<float> > vertices (1, (*this)[index]);
  const float step_sq = Math::pow2 (step_size);
  for (int32_t step = -1; step <= 1; step += 2) {
    if (step == 1) {
      std::reverse (vertices.begin(), vertices.end());
      index = (size() + 1) / 2;
    }
    do {
      while ((index+step) >= 0 && (index+step) < int32_t(size()) && dist2 ((*this)[index+step], vertices.back()) < step_sq)
        index += step;
      // Ideal point for fixed step size lies somewhere between [index] and [index+step]
      // Do a binary search to find this point
      // Unless we're at an endpoint...
      if (index == 0 || index == int32_t(size())-1) {
        vertices.push_back ((*this)[index]);
      } else {
        float lower = 0.0f, mu = 0.5f, upper = 1.0f;
        Point<float> p (((*this)[index] + (*this)[index+step]) * 0.5f);
        for (uint32_t iter = 0; iter != 6; ++iter) {
          if (dist2 (p, vertices.back()) > step_sq)
            upper = mu;
          else
            lower = mu;
          mu = 0.5 * (lower + upper);
          p = ((*this)[index] * (1.0-mu)) + ((*this)[index+step] * mu);
        }
        vertices.push_back (p);
      }
    } while (index != 0 && index != int32_t(size())-1);
  }
  std::swap (*this, vertices);

  is_finalized = true;
}


}
}
}
}


