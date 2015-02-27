// Filename: StaggeredStokesPhysicalBoundaryHelper.cpp
// Created on 28 Aug 2012 by Boyce Griffith
//
// Copyright (c) 2002-2014, Boyce Griffith
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of The University of North Carolina nor the names of
//      its contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/////////////////////////////// INCLUDES /////////////////////////////////////

#include <stddef.h>
#include <map>
#include <ostream>
#include <utility>
#include <vector>

#include "SAMRAI/pdat/ArrayData.h"
#include "SAMRAI/hier/BoundaryBox.h"
#include "SAMRAI/hier/Box.h"
#include "SAMRAI/hier/Index.h"
#include "SAMRAI/hier/IntVector.h"
#include "SAMRAI/hier/Patch.h"
#include "SAMRAI/hier/PatchGeometry.h"
#include "SAMRAI/hier/PatchHierarchy.h"
#include "SAMRAI/hier/PatchLevel.h"
#include "SAMRAI/solv/RobinBcCoefStrategy.h"
#include "SAMRAI/pdat/SideData.h"
#include "SAMRAI/pdat/SideIndex.h"
#include "SAMRAI/hier/Variable.h"
#include "ibamr/StaggeredStokesPhysicalBoundaryHelper.h"
#include "ibamr/StokesBcCoefStrategy.h"
#include "ibamr/ibamr_utilities.h"
#include "ibamr/namespaces.h" // IWYU pragma: keep
#include "ibtk/ExtendedRobinBcCoefStrategy.h"
#include "ibtk/StaggeredPhysicalBoundaryHelper.h"
#include "SAMRAI/tbox/Array.h"
#include "SAMRAI/tbox/MathUtilities.h"

#include "SAMRAI/tbox/Utilities.h"

/////////////////////////////// NAMESPACE ////////////////////////////////////

namespace IBAMR
{
/////////////////////////////// STATIC ///////////////////////////////////////

/////////////////////////////// PUBLIC ///////////////////////////////////////

StaggeredStokesPhysicalBoundaryHelper::StaggeredStokesPhysicalBoundaryHelper()
{
    // intentionally blank
    return;
} // StaggeredStokesPhysicalBoundaryHelper

StaggeredStokesPhysicalBoundaryHelper::~StaggeredStokesPhysicalBoundaryHelper()
{
    // intentionally blank
    return;
} // ~StaggeredStokesPhysicalBoundaryHelper

void StaggeredStokesPhysicalBoundaryHelper::enforceNormalVelocityBoundaryConditions(
    const int u_data_idx,
    const int p_data_idx,
    const std::vector<RobinBcCoefStrategy*>& u_bc_coefs,
    const double fill_time,
    const bool homogeneous_bc,
    const int coarsest_ln,
    const int finest_ln) const
{
    TBOX_ASSERT(u_bc_coefs.size() == NDIM);
    TBOX_ASSERT(d_hierarchy);
    StaggeredStokesPhysicalBoundaryHelper::setupBcCoefObjects(
        u_bc_coefs, /*p_bc_coef*/ NULL, u_data_idx, p_data_idx, homogeneous_bc);
    std::vector<int> target_data_idxs(2);
    target_data_idxs[0] = u_data_idx;
    target_data_idxs[1] = p_data_idx;
    const int finest_hier_level = d_hierarchy->getFinestLevelNumber();
    for (int ln = (coarsest_ln == -1 ? 0 : coarsest_ln); ln <= (finest_ln == -1 ? finest_hier_level : finest_ln); ++ln)
    {
        boost::shared_ptr<PatchLevel> level = d_hierarchy->getPatchLevel(ln);
        for (PatchLevel::iterator p = level->begin(); p != level->end(); ++p)
        {
            boost::shared_ptr<Patch> patch = *p;
            const int patch_id = patch->getGlobalId().getLocalId().getValue();
            boost::shared_ptr<PatchGeometry> pgeom = patch->getPatchGeometry();
            if (pgeom->getTouchesRegularBoundary())
            {
                boost::shared_ptr<SideData<double> > u_data = patch->getPatchData(u_data_idx);
                Box bc_coef_box(DIM);
                BoundaryBox trimmed_bdry_box(DIM);
                const std::vector<BoundaryBox>& physical_codim1_boxes =
                    d_physical_codim1_boxes[ln].find(patch_id)->second;
                const int n_physical_codim1_boxes = physical_codim1_boxes.size();
                for (int n = 0; n < n_physical_codim1_boxes; ++n)
                {
                    const BoundaryBox& bdry_box = physical_codim1_boxes[n];
                    StaggeredPhysicalBoundaryHelper::setupBcCoefBoxes(bc_coef_box, trimmed_bdry_box, bdry_box, patch);
                    const unsigned int bdry_normal_axis = bdry_box.getLocationIndex() / 2;
                    auto acoef_data = boost::make_shared<ArrayData<double> >(bc_coef_box, 1);
                    auto bcoef_data = boost::make_shared<ArrayData<double> >(bc_coef_box, 1);
                    auto gcoef_data = boost::make_shared<ArrayData<double> >(bc_coef_box, 1);
                    u_bc_coefs[bdry_normal_axis]->setBcCoefs(acoef_data,
                                                             bcoef_data,
                                                             gcoef_data,
                                                             boost::shared_ptr<Variable>(),
                                                             *patch,
                                                             trimmed_bdry_box,
                                                             fill_time);
                    auto extended_bc_coef = dynamic_cast<ExtendedRobinBcCoefStrategy*>(u_bc_coefs[bdry_normal_axis]);
                    if (homogeneous_bc && !extended_bc_coef) gcoef_data->fillAll(0.0);
                    for (Box::Iterator it(bc_coef_box); it; it++)
                    {
                        const Index& i = it();
                        const double& alpha = (*acoef_data)(i, 0);
                        const double& beta = (*bcoef_data)(i, 0);
                        const double gamma = homogeneous_bc && !extended_bc_coef ? 0.0 : (*gcoef_data)(i, 0);
                        TBOX_ASSERT(MathUtilities<double>::equalEps(alpha + beta, 1.0));
                        TBOX_ASSERT(MathUtilities<double>::equalEps(alpha, 1.0) ||
                                    MathUtilities<double>::equalEps(beta, 1.0));
                        if (MathUtilities<double>::equalEps(alpha, 1.0))
                            (*u_data)(SideIndex(i, bdry_normal_axis, SideIndex::Lower)) = gamma;
                    }
                }
            }
        }
    }
    StaggeredStokesPhysicalBoundaryHelper::resetBcCoefObjects(u_bc_coefs, /*p_bc_coef*/ NULL);
    return;
} // enforceNormalVelocityBoundaryConditions

#if 0
void
StaggeredStokesPhysicalBoundaryHelper::enforceDivergenceFreeConditionAtBoundary(
    const int u_data_idx,
    const int coarsest_ln,
    const int finest_ln) const
{
    TBOX_ASSERT(d_hierarchy);
    const int finest_hier_level = d_hierarchy->getFinestLevelNumber();
    for (int ln = (coarsest_ln == -1 ? 0 : coarsest_ln); ln <= (finest_ln == -1 ? finest_hier_level : finest_ln); ++ln)
    {
        boost::shared_ptr<PatchLevel > level = d_hierarchy->getPatchLevel(ln);
        for (PatchLevel::iterator p = level->begin(); p != level->end(); ++p)
        {
            boost::shared_ptr<Patch > patch = *p;
            if (patch->getPatchGeometry()->getTouchesRegularBoundary())
            {
                boost::shared_ptr<SideData<double> > u_data = patch->getPatchData(u_data_idx);
                enforceDivergenceFreeConditionAtBoundary(u_data, patch);
            }
        }
    }
    return;
}// enforceDivergenceFreeConditionAtBoundary

void
StaggeredStokesPhysicalBoundaryHelper::enforceDivergenceFreeConditionAtBoundary(
    boost::shared_ptr<SideData<double> > u_data,
    boost::shared_ptr<Patch > patch) const
{
    if (!patch->getPatchGeometry()->getTouchesRegularBoundary()) return;
    const int ln = patch->getPatchLevelNumber();
    const GlobalId& patch_id = patch->getGlobalId();
    boost::shared_ptr<CartesianPatchGeometry > pgeom = patch->getPatchGeometry();
    const double* const dx = pgeom->getDx();
    const std::vector<BoundaryBox >& physical_codim1_boxes = d_physical_codim1_boxes[ln].find(patch_num)->second;
    const int n_physical_codim1_boxes = physical_codim1_boxes.size();
    const std::vector<boost::shared_ptr<ArrayData<bool> > >& dirichlet_bdry_locs = d_dirichlet_bdry_locs[ln].find(patch_num)->second;
    for (int n = 0; n < n_physical_codim1_boxes; ++n)
    {
        const BoundaryBox& bdry_box   = physical_codim1_boxes[n];
        const unsigned int location_index   = bdry_box.getLocationIndex();
        const unsigned int bdry_normal_axis = location_index / 2;
        const bool is_lower                 = location_index % 2 == 0;
        const Box& bc_coef_box        = dirichlet_bdry_locs[n]->getBox();
        const ArrayData<bool>& bdry_locs_data = *dirichlet_bdry_locs[n];
        for (Box::Iterator it(bc_coef_box); it; it++)
        {
            const Index& i = it();
            if (!bdry_locs_data(i,0))
            {
                // Place i_g in the ghost cell abutting the boundary.
                Index i_g = i;
                if (is_lower)
                {
                    i_g(bdry_normal_axis) -= 1;
                }
                else
                {
                    // intentionally blank
                }

                // Work out from the physical boundary to fill the ghost cell
                // values so that the velocity field satisfies the discrete
                // divergence-free condition.
                for (int k = 0; k < u_data->getGhostCellWidth()(bdry_normal_axis); ++k, i_g(bdry_normal_axis) += (is_lower ? -1 : +1))
                {
                    // Determine the ghost cell value so that the divergence of
                    // the velocity field is zero in the ghost cell.
                    SideIndex i_g_s(i_g, bdry_normal_axis, is_lower ? SideIndex::Lower : SideIndex::Upper);
                    (*u_data)(i_g_s) = 0.0;
                    double div_u_g = 0.0;
                    for (unsigned int axis = 0; axis < NDIM; ++axis)
                    {
                        const SideIndex i_g_s_upper(i_g,axis,SideIndex::Upper);
                        const SideIndex i_g_s_lower(i_g,axis,SideIndex::Lower);
                        div_u_g += ((*u_data)(i_g_s_upper)-(*u_data)(i_g_s_lower))*dx[bdry_normal_axis]/dx[axis];
                    }
                    (*u_data)(i_g_s) = (is_lower ? +1.0 : -1.0)*div_u_g;
                }
            }
        }
    }
    return;
}// enforceDivergenceFreeConditionAtBoundary
#endif

void StaggeredStokesPhysicalBoundaryHelper::setupBcCoefObjects(const std::vector<RobinBcCoefStrategy*>& u_bc_coefs,
                                                               RobinBcCoefStrategy* p_bc_coef,
                                                               int u_target_data_idx,
                                                               int p_target_data_idx,
                                                               bool homogeneous_bc)
{
    TBOX_ASSERT(u_bc_coefs.size() == NDIM);
    for (unsigned int d = 0; d < NDIM; ++d)
    {
        auto extended_u_bc_coef = dynamic_cast<ExtendedRobinBcCoefStrategy*>(u_bc_coefs[d]);
        if (extended_u_bc_coef)
        {
            extended_u_bc_coef->clearTargetPatchDataIndex();
            extended_u_bc_coef->setHomogeneousBc(homogeneous_bc);
        }
        StokesBcCoefStrategy* stokes_u_bc_coef = dynamic_cast<StokesBcCoefStrategy*>(u_bc_coefs[d]);
        if (stokes_u_bc_coef)
        {
            stokes_u_bc_coef->setTargetVelocityPatchDataIndex(u_target_data_idx);
            stokes_u_bc_coef->setTargetPressurePatchDataIndex(p_target_data_idx);
        }
    }
    auto extended_p_bc_coef = dynamic_cast<ExtendedRobinBcCoefStrategy*>(p_bc_coef);
    if (extended_p_bc_coef)
    {
        extended_p_bc_coef->clearTargetPatchDataIndex();
        extended_p_bc_coef->setHomogeneousBc(homogeneous_bc);
    }
    StokesBcCoefStrategy* stokes_p_bc_coef = dynamic_cast<StokesBcCoefStrategy*>(p_bc_coef);
    if (stokes_p_bc_coef)
    {
        stokes_p_bc_coef->setTargetVelocityPatchDataIndex(u_target_data_idx);
        stokes_p_bc_coef->setTargetPressurePatchDataIndex(p_target_data_idx);
    }
    return;
} // setupBcCoefObjects

void StaggeredStokesPhysicalBoundaryHelper::resetBcCoefObjects(const std::vector<RobinBcCoefStrategy*>& u_bc_coefs,
                                                               RobinBcCoefStrategy* p_bc_coef)
{
    TBOX_ASSERT(u_bc_coefs.size() == NDIM);
    for (unsigned int d = 0; d < NDIM; ++d)
    {
        auto stokes_u_bc_coef = dynamic_cast<StokesBcCoefStrategy*>(u_bc_coefs[d]);
        if (stokes_u_bc_coef)
        {
            stokes_u_bc_coef->clearTargetVelocityPatchDataIndex();
            stokes_u_bc_coef->clearTargetPressurePatchDataIndex();
        }
    }
    auto stokes_p_bc_coef = dynamic_cast<StokesBcCoefStrategy*>(p_bc_coef);
    if (stokes_p_bc_coef)
    {
        stokes_p_bc_coef->clearTargetVelocityPatchDataIndex();
        stokes_p_bc_coef->clearTargetPressurePatchDataIndex();
    }
    return;
} // resetBcCoefObjects

/////////////////////////////// PROTECTED ////////////////////////////////////

/////////////////////////////// PRIVATE //////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////

} // namespace IBAMR

//////////////////////////////////////////////////////////////////////////////
