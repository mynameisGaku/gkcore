// SPDX-License-Identifier: NOASSERTION
#include <gkcore/FModelSecondaryMotionContactPoint.h>
#include <gkcore/FModelSecondaryMotionContactAnchor.h>
#include "model/animation/ModelSecondaryMotionWeightedContacts.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionDirection.h"
#include "model/animation/ModelSecondaryMotionChainContacts.h"
#include "foundation/Memory.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
using namespace gk::model::animation;

bool Fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return false;
}

double Distance(const gk::FVector3d& first, const gk::FVector3d& second)
{
    const double x = second.value[0] - first.value[0];
    const double y = second.value[1] - first.value[1];
    const double z = second.value[2] - first.value[2];
    return sqrt(x * x + y * y + z * z);
}

gk::FVector3d WeightedPosition(const gk::FVector3d& first, const gk::FVector3d& second, double firstWeight, double secondWeight)
{
    return { { first.value[0] * firstWeight + second.value[0] * secondWeight, first.value[1] * firstWeight + second.value[1] * secondWeight, first.value[2] * firstWeight + second.value[2] * secondWeight } };
}

gk::FVector3d EvaluateAnchorPoint(const FSecondaryMotionWeightedContactAnchor& anchor, const gk::Array<gk::FVector3d>& positions)
{
    if (anchor.chainIndex == 0xFFFFFFFFu)
    {
        return anchor.constantPosition;
    }
    const uint32_t rootIndex = anchor.chainIndex * 2u;
    const auto& parent = positions.At(rootIndex + anchor.segmentIndex);
    const auto& child = positions.At(rootIndex + anchor.segmentIndex + 1u);
    const double dx = child.value[0] - parent.value[0];
    const double dy = child.value[1] - parent.value[1];
    const double dz = child.value[2] - parent.value[2];
    const double length = sqrt(dx * dx + dy * dy + dz * dz);
    const double direction[3] = { dx / length, dy / length, dz / length };
    const double* base = anchor.definition.direction.value;
    const double baseLength = sqrt(base[0] * base[0] + base[1] * base[1] + base[2] * base[2]);
    const double u[3] = { base[0] / baseLength, base[1] / baseLength, base[2] / baseLength };
    const double axis[3] = { u[1] * direction[2] - u[2] * direction[1], u[2] * direction[0] - u[0] * direction[2], u[0] * direction[1] - u[1] * direction[0] };
    const double cosine = u[0] * direction[0] + u[1] * direction[1] + u[2] * direction[2];
    const double firstCross[3] = { axis[1] * anchor.definition.offset.value[2] - axis[2] * anchor.definition.offset.value[1], axis[2] * anchor.definition.offset.value[0] - axis[0] * anchor.definition.offset.value[2], axis[0] * anchor.definition.offset.value[1] - axis[1] * anchor.definition.offset.value[0] };
    const double secondCross[3] = { axis[1] * firstCross[2] - axis[2] * firstCross[1], axis[2] * firstCross[0] - axis[0] * firstCross[2], axis[0] * firstCross[1] - axis[1] * firstCross[0] };
    const double denominator = 1.0 + cosine;
    return { { parent.value[0] + anchor.definition.offset.value[0] + firstCross[0] + secondCross[0] / denominator, parent.value[1] + anchor.definition.offset.value[1] + firstCross[1] + secondCross[1] / denominator, parent.value[2] + anchor.definition.offset.value[2] + firstCross[2] + secondCross[2] / denominator } };
}

bool IsSolved(const gk::Array<gk::FVector3d>& positions, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FPreparedSecondaryMotionCollisionShape& body, const gk::FVector3d& fixedFirst, const gk::FVector3d& fixedSecond)
{
    if (positions.Count() != 4u || Distance(positions.At(0), fixedFirst) > 1.0e-12 || Distance(positions.At(2), fixedSecond) > 1.0e-12)
    {
        return false;
    }
    const gk::FVector3d referenceDirections[2] = { { { 1.0, 0.0, 0.0 } }, { { -1.0, 0.0, 0.0 } } };
    for (uint32_t chain = 0; chain < 2u; ++chain)
    {
        const auto& root = positions.At(chain * 2u);
        const auto& child = positions.At(chain * 2u + 1u);
        const double length = Distance(root, child);
        const double directionDot = ((child.value[0] - root.value[0]) * referenceDirections[chain].value[0] + (child.value[1] - root.value[1]) * referenceDirections[chain].value[1] + (child.value[2] - root.value[2]) * referenceDirections[chain].value[2]) / length;
        if (fabs(length - 1.0) > 0.0011 || directionDot < -0.001)
        {
            return false;
        }
    }
    gk::FVector3d mixed{};
    double weightSum = 0.0;
    for (uint32_t index = 0; index < anchorCount; ++index)
    {
        const auto& anchor = anchors[index];
        if (anchor.chainIndex == 0xFFFFFFFFu)
        {
            mixed.value[0] += anchor.constantPosition.value[0] * anchor.weight;
            mixed.value[1] += anchor.constantPosition.value[1] * anchor.weight;
            mixed.value[2] += anchor.constantPosition.value[2] * anchor.weight;
        }
        else
        {
            const auto position = EvaluateAnchorPoint(anchor, positions);
            mixed.value[0] += position.value[0] * anchor.weight;
            mixed.value[1] += position.value[1] * anchor.weight;
            mixed.value[2] += position.value[2] * anchor.weight;
        }
        weightSum += anchor.weight;
    }
    (void)body;
    const gk::FVector3d bodyCenter = { { 0.0, 0.0, 0.0 } };
    return fabs(weightSum - 1.0) <= 1.0e-12 && Distance(mixed, bodyCenter) >= body.radius * 0.998;
}

/**
 * 鎖の姿勢からweighted接触anchorの回転offset位置を求める。
 */
gk::FVector3d EvaluateCapturedFixtureAnchorPoint(const FSecondaryMotionWeightedContactAnchor& anchor, const FSecondaryMotionContactChain* chains, const gk::Array<gk::FVector3d>& positions)
{
    if (anchor.chainIndex == 0xFFFFFFFFu)
    {
        return anchor.constantPosition;
    }
    const FSecondaryMotionContactChain& chain = chains[anchor.chainIndex];
    const gk::FVector3d& parent = positions.At(chain.firstPoint + anchor.segmentIndex);
    const gk::FVector3d& child = positions.At(chain.firstPoint + anchor.segmentIndex + 1u);
    const double dx = child.value[0] - parent.value[0];
    const double dy = child.value[1] - parent.value[1];
    const double dz = child.value[2] - parent.value[2];
    const double length = sqrt(dx * dx + dy * dy + dz * dz);
    const double direction[3] = { dx / length, dy / length, dz / length };
    const double* base = anchor.definition.direction.value;
    const double baseLength = sqrt(base[0] * base[0] + base[1] * base[1] + base[2] * base[2]);
    const double u[3] = { base[0] / baseLength, base[1] / baseLength, base[2] / baseLength };
    const double axis[3] = { u[1] * direction[2] - u[2] * direction[1], u[2] * direction[0] - u[0] * direction[2], u[0] * direction[1] - u[1] * direction[0] };
    const double cosine = u[0] * direction[0] + u[1] * direction[1] + u[2] * direction[2];
    const double* offset = anchor.definition.offset.value;
    const double firstCross[3] = { axis[1] * offset[2] - axis[2] * offset[1], axis[2] * offset[0] - axis[0] * offset[2], axis[0] * offset[1] - axis[1] * offset[0] };
    const double secondCross[3] = { axis[1] * firstCross[2] - axis[2] * firstCross[1], axis[2] * firstCross[0] - axis[0] * firstCross[2], axis[0] * firstCross[1] - axis[1] * firstCross[0] };
    const double denominator = 1.0 + cosine;
    return { { parent.value[0] + offset[0] + firstCross[0] + secondCross[0] / denominator, parent.value[1] + offset[1] + firstCross[1] + secondCross[1] / denominator, parent.value[2] + offset[2] + firstCross[2] + secondCross[2] / denominator } };
}

/**
 * float保存後のroot、骨長、cone、身体span、weighted接触を確認する。
 */
bool HasCapturedFixtureConstraints(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::FVector3d* input, const gk::Array<gk::FVector3d>& positions, gk::String& error)
{
    // 末尾の鎖までの位置数を、異なる鎖数のfixtureにも合わせる。
    const uint32_t expectedPointCount = chainCount == 0u ? 0u : chains[chainCount - 1u].firstPoint + chains[chainCount - 1u].segmentCount + 1u;
    if (positions.Count() != expectedPointCount)
    {
        return false;
    }
    gk::Array<gk::FVector3d> storedPositions;
    if (!storedPositions.Reserve(positions.Count()))
    {
        return false;
    }
    for (uint32_t index = 0; index < positions.Count(); ++index)
    {
        gk::FVector3d stored{};
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            stored.value[axis] = static_cast<float>(positions.At(index).value[axis]);
        }
        if (!storedPositions.Append(stored))
        {
            return false;
        }
    }
    for (uint32_t ci = 0; ci < chainCount; ++ci)
    {
        const FSecondaryMotionContactChain& chain = chains[ci];
        if (Distance(storedPositions.At(chain.firstPoint), input[chain.firstPoint]) > 1.0e-12)
        {
            return false;
        }
        for (uint32_t seg = 0; seg < chain.segmentCount; ++seg)
        {
            const uint32_t ri = chain.firstSegment + seg;
            const auto& p = storedPositions.At(chain.firstPoint + seg);
            const auto& q = storedPositions.At(chain.firstPoint + seg + 1u);
            const double length = Distance(p, q);
            if (!(length > 0.0) || fabs(length - references[ri].length) > references[ri].length * (128.0 * FLT_EPSILON))
            {
                return false;
            }
            const double dot = ((q.value[0] - p.value[0]) * references[ri].direction.value[0] + (q.value[1] - p.value[1]) * references[ri].direction.value[1] + (q.value[2] - p.value[2]) * references[ri].direction.value[2]) / length;
            const double storedConeLimit = cos(fmin(3.14159265358979323846, acos(chain.cosineLimit) + 128.0 * FLT_EPSILON));
            if (dot < storedConeLimit)
            {
                return false;
            }
        }
        gk::Array<gk::FVector3d> gradients;
        if (!gradients.Reserve(chain.segmentCount))
        {
            return false;
        }
        for (uint32_t seg = 0; seg < chain.segmentCount; ++seg)
        {
            if (!gradients.Append(gk::FVector3d{}))
            {
                return false;
            }
        }
        double bodyMerit = 0.0;
        if (!EvaluatePreparedSecondaryMotionChainContactsMerit(chain.shapes, chain.shapeCount, references + chain.firstSegment, chain.segmentCount, chain.cosineLimit, 1.0, nullptr, 0u, storedPositions.Data() + chain.firstPoint, chain.segmentCount + 1u, bodyMerit, gradients.Data(), gradients.Count(), error) || bodyMerit != 0.0)
        {
            return false;
        }
        for (uint32_t shapeIndex = 0; shapeIndex < chain.shapeCount; ++shapeIndex)
        {
            const auto& shape = chain.shapes[shapeIndex];
            const double rootThreshold = shape.hasRootContactOverride ? GetPreparedSecondaryMotionRootContactThreshold(shape) : 0.0;
            for (uint32_t seg = 0; seg < chain.segmentCount; ++seg)
            {
                const auto& first = storedPositions.At(chain.firstPoint + seg);
                const auto& second = storedPositions.At(chain.firstPoint + seg + 1u);
                double penetration = 0.0;
                double spanAmount = 0.0;
                gk::FVector3d normal{};
                if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, second, second, penetration, spanAmount, normal, error, 1.0) || penetration > 0.0)
                {
                    return false;
                }
                bool skipSpan = false;
                double spanTolerance = 1.0;
                double rootSpanThreshold = 0.0;
                if (seg == 0u)
                {
                    if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, first, first, penetration, spanAmount, normal, error, 0.0))
                    {
                        return false;
                    }
                    const double rootDistance = shape.radius - penetration;
                    skipSpan = shape.hasRootContactOverride ? penetration > shape.radius - rootThreshold : penetration > shape.tolerance;
                    if (shape.hasRootContactOverride)
                    {
                        rootSpanThreshold = GetPreparedSecondaryMotionRootSpanThreshold(shape, rootDistance, 1.0);
                        spanTolerance = 0.0;
                    }
                }
                if (!skipSpan)
                {
                    if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, first, second, penetration, spanAmount, normal, error, spanTolerance))
                    {
                        return false;
                    }
                    if (seg == 0u && shape.hasRootContactOverride)
                    {
                        penetration += rootSpanThreshold - shape.radius;
                    }
                    if (penetration > 0.0)
                    {
                        return false;
                    }
                }
            }
        }
    }
    for (uint32_t ci = 0; ci < contactCount; ++ci)
    {
        const auto& contact = contacts[ci];
        gk::FVector3d center{};
        double weightSum = 0.0;
        for (uint32_t local = 0; local < contact.anchorCount; ++local)
        {
            const auto& anchor = anchors[contact.firstAnchor + local];
            const auto point = EvaluateCapturedFixtureAnchorPoint(anchor, chains, storedPositions);
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                center.value[axis] += point.value[axis] * anchor.weight;
            }
            weightSum += anchor.weight;
        }
        if (fabs(weightSum - 1.0) > 1.0e-6)
        {
            return false;
        }
        for (uint32_t si = 0; si < contact.shapeCount; ++si)
        {
            double penetration = 0.0, spanAmount = 0.0;
            gk::FVector3d normal{};
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(contact.shapes[si], center, center, penetration, spanAmount, normal, error, 0.5) || penetration > 0.0)
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * 既知のclear姿勢を保ち、縮小したweighted接触群の全制約を検査する。
 */
bool TestCapturedWeightedContactCluster()
{
    const FPreparedSecondaryMotionCollisionShape collisionShapes[12] = { { { { 0.033586930483579636, 1.2063159942626953, 2.2247331142425537 } }, { { 0.18234299123287201, 1.0596176385879517, 2.2034909725189209 } }, { { 0.14875606074929237, -0.14669835567474365, -0.021242141723632812 } }, 0.044100001752327658, 0.21000000417220868, 0.088088005781173706, 8.8088005781173704e-05, true, 0.087912006966769693, { -0.05450107529759407, 0.97152963280677795, 2.1154029667377472 }, { 0.27043099701404572, 1.294404000043869, 2.3128211200237274 }, { -0.054501075297594077, 0.97152963280677784, 2.1154029667377467 }, { 0.27043099701404577, 1.2944040000438692, 2.3128211200237279 } }, { { { -0.028980756178498268, 1.2340618371963501, 2.347308874130249 } }, { { 0.12450264394283295, 1.2136865854263306, 2.4891810417175293 } }, { { 0.15348340012133121, -0.020375251770019531, 0.14187216758728027 } }, 0.044100016933409675, 0.21000004031763825, 0.088087998330593109, 8.8087998330593106e-05, true, 0.087911999523639678, { -0.11706875450909138, 1.1255985870957375, 2.2592208757996559 }, { 0.21259064227342606, 1.3221498355269432, 2.5772690400481224 }, { -0.11706875450909139, 1.1255985870957372, 2.2592208757996555 }, { 0.21259064227342608, 1.3221498355269434, 2.5772690400481229 } }, { { { 0.19550915062427521, 1.011541485786438, 2.1864137649536133 } }, { { 0.090956293046474457, 0.77542489767074585, 2.0544378757476807 } }, { { -0.10455285757780075, -0.23611658811569214, -0.13197588920593262 } }, 0.084099978542775899, 0.2899999630047837, 0.078078001737594604, 7.8078001737594599e-05, true, 0.07792200160771609, { 0.012878291308879852, 0.69734689593315125, 1.9763598740100861 }, { 0.27358715236186981, 1.0896194875240326, 2.2644917666912079 }, { 0.012878291308879851, 0.69734689593315113, 1.9763598740100858 }, { 0.27358715236186987, 1.0896194875240328, 2.2644917666912083 } }, { { { 0.13835740089416504, 1.1833007335662842, 2.5052003860473633 } }, { { 0.033094227313995361, 0.92132443189620972, 2.4389579296112061 } }, { { -0.10526317358016968, -0.26197630167007446, -0.066242456436157227 } }, 0.084099981383594979, 0.28999996790274818, 0.078078001737594604, 7.8078001737594599e-05, true, 0.07792200160771609, { -0.044983774423599243, 0.84324643015861511, 2.3608799278736115 }, { 0.21643540263175964, 1.2613787353038788, 2.5832783877849579 }, { -0.04498377442359925, 0.843246430158615, 2.360879927873611 }, { 0.21643540263175967, 1.261378735303879, 2.5832783877849583 } }, { { { 0.017042968422174454, 1.2351510524749756, 2.3002519607543945 } }, { { 0.017042968422174454, 1.2351510524749756, 2.3002519607543945 } }, { { 0, 0, 0 } }, 0, 0, 0.072000004351139069, 7.2000004351139066e-05, false, 0, { -0.054957035928964615, 1.1631510481238365, 2.2282519564032555 }, { 0.089042972773313522, 1.3071510568261147, 2.3722519651055336 }, { -0.054957035928964622, 1.1631510481238363, 2.228251956403255 }, { 0.089042972773313536, 1.3071510568261149, 2.372251965105534 } }, { { { 0.049602504819631577, 1.2749863862991333, 2.3171019554138184 } }, { { 0.13458633422851562, 1.3245565891265869, 2.3663010597229004 } }, { { 0.084983829408884048, 0.049570202827453613, 0.049199104309082031 } }, 0.01210000813416913, 0.11000003697348983, 0.082000009715557098, 8.2000009715557095e-05, false, 0, { -0.032397504895925522, 1.1929863765835762, 2.2351019456982613 }, { 0.21658634394407272, 1.406556598842144, 2.4483010694384575 }, { -0.032397504895925529, 1.192986376583576, 2.2351019456982608 }, { 0.21658634394407275, 1.4065565988421442, 2.4483010694384579 } }, { { { 0.14156474173069, 1.3286269903182983, 2.3703408241271973 } }, { { 0.23922786116600037, 1.3991998434066772, 2.4230797290802002 } }, { { 0.097663119435310364, 0.070572853088378906, 0.05273890495300293 } }, 0.017300004586511486, 0.13152948181495844, 0.082000009715557098, 8.2000009715557095e-05, false, 0, { 0.059564732015132904, 1.2466269806027412, 2.2883408144116402 }, { 0.32122787088155746, 1.4811998531222343, 2.5050797387957573 }, { 0.059564732015132897, 1.246626980602741, 2.2883408144116397 }, { 0.32122787088155752, 1.4811998531222346, 2.5050797387957577 } }, { { { 0.36000248789787292, 1.4480782747268677, 2.4838740825653076 } }, { { 0.36000248789787292, 1.4480782747268677, 2.4838740825653076 } }, { { 0, 0, 0 } }, 0, 0, 0.092000007629394531, 9.2000007629394539e-05, false, 0, { 0.26800248026847839, 1.3560782670974731, 2.3918740749359131 }, { 0.45200249552726746, 1.5400782823562622, 2.5758740901947021 }, { 0.26800248026847834, 1.3560782670974729, 2.3918740749359126 }, { 0.45200249552726751, 1.5400782823562624, 2.5758740901947026 } }, { { { 0.033586930483579636, 1.2063159942626953, 2.2247331142425537 } }, { { 0.18234299123287201, 1.0596176385879517, 2.2034909725189209 } }, { { 0.14875606074929237, -0.14669835567474365, -0.021242141723632812 } }, 0.044100001752327658, 0.21000000417220868, 0.052000008523464203, 5.2000008523464205e-05, false, 0, { -0.018413078039884567, 1.0076176300644875, 2.1514909639954567 }, { 0.23434299975633621, 1.2583160027861595, 2.2767331227660179 }, { -0.018413078039884571, 1.0076176300644872, 2.1514909639954563 }, { 0.23434299975633624, 1.2583160027861597, 2.2767331227660184 } }, { { { -0.028980756178498268, 1.2340618371963501, 2.347308874130249 } }, { { 0.12450264394283295, 1.2136865854263306, 2.4891810417175293 } }, { { 0.15348340012133121, -0.020375251770019531, 0.14187216758728027 } }, 0.044100016933409675, 0.21000004031763825, 0.052000001072883606, 5.2000001072883607e-05, false, 0, { -0.080980757251381874, 1.161686584353447, 2.2953088730573654 }, { 0.17650264501571655, 1.2860618382692337, 2.5411810427904129 }, { -0.080980757251381888, 1.1616865843534467, 2.295308873057365 }, { 0.17650264501571658, 1.2860618382692339, 2.5411810427904133 } }, { { { 0.19550915062427521, 1.011541485786438, 2.1864137649536133 } }, { { 0.090956293046474457, 0.77542489767074585, 2.0544378757476807 } }, { { -0.10455285757780075, -0.23611658811569214, -0.13197588920593262 } }, 0.084099978542775899, 0.2899999630047837, 0.041999999433755875, 4.1999999433755877e-05, false, 0, { 0.048956293612718582, 0.73342489823698997, 2.0124378763139248 }, { 0.23750915005803108, 1.0535414852201939, 2.2284137643873692 }, { 0.048956293612718575, 0.73342489823698986, 2.0124378763139243 }, { 0.23750915005803111, 1.0535414852201941, 2.2284137643873696 } }, { { { 0.13835740089416504, 1.1833007335662842, 2.5052003860473633 } }, { { 0.033094227313995361, 0.92132443189620972, 2.4389579296112061 } }, { { -0.10526317358016968, -0.26197630167007446, -0.066242456436157227 } }, 0.084099981383594979, 0.28999996790274818, 0.041999999433755875, 4.1999999433755877e-05, false, 0, { -0.0089057721197605133, 0.87932443246245384, 2.3969579301774502 }, { 0.18035740032792091, 1.2253007330000401, 2.5472003854811192 }, { -0.008905772119760515, 0.87932443246245373, 2.3969579301774497 }, { 0.18035740032792094, 1.2253007330000403, 2.5472003854811196 } } };
    for (uint32_t shapeIndex = 0; shapeIndex < 12u; ++shapeIndex)
    {
        const auto& shape = collisionShapes[shapeIndex];
        double axisLengthSquared = 0.0;
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            const double expectedAxis = shape.end.value[axis] - shape.start.value[axis];
            const double expectedLow = fmin(shape.start.value[axis], shape.end.value[axis]) - shape.radius;
            const double expectedHigh = fmax(shape.start.value[axis], shape.end.value[axis]) + shape.radius;
            if (!isfinite(shape.start.value[axis]) || !isfinite(shape.end.value[axis]) || !isfinite(shape.axis.value[axis]) || !isfinite(shape.boundsLow[axis]) || !isfinite(shape.boundsHigh[axis]) || !isfinite(shape.conservativeBoundsLow[axis]) || !isfinite(shape.conservativeBoundsHigh[axis]) || fabs(shape.axis.value[axis] - expectedAxis) > 1.0e-12 || fabs(shape.boundsLow[axis] - expectedLow) > 1.0e-12 || fabs(shape.boundsHigh[axis] - expectedHigh) > 1.0e-12 || shape.conservativeBoundsLow[axis] > shape.boundsLow[axis] || shape.conservativeBoundsHigh[axis] < shape.boundsHigh[axis])
            {
                return Fail("the captured prepared-shape bounds or axis are inconsistent");
            }
            axisLengthSquared += shape.axis.value[axis] * shape.axis.value[axis];
        }
        if (!(shape.radius > 0.0) || !(shape.tolerance >= 0.0) || shape.tolerance > shape.radius || fabs(shape.axisLengthSquared - axisLengthSquared) > 1.0e-12 || fabs(shape.axisLength - sqrt(axisLengthSquared)) > 1.0e-12 || (shape.hasRootContactOverride && (!(shape.rootContactThreshold >= 0.0) || shape.rootContactThreshold > shape.radius)))
        {
            return Fail("the captured prepared-shape dimensions are inconsistent");
        }
    }
    FSecondaryMotionContactChain chains[4]{};
    for (uint32_t ci = 0; ci < 4u; ++ci)
    {
        chains[ci].firstPoint = ci * 5u;
        chains[ci].firstSegment = ci * 4u;
        chains[ci].segmentCount = 4u;
        chains[ci].cosineLimit = -1.0;
        chains[ci].shapes = collisionShapes;
        chains[ci].shapeCount = 4u;
    }
    const FSecondaryMotionReferenceSegment references[16] = { { { { -0.52493285865791772, -0.8120235580617946, -0.25507495967634691 } }, 0.093607406511327124 }, { { { -0.52307487313420853, -0.81364143218233309, -0.25373272735678642 } }, 0.062305952045502142 }, { { { -0.55582133868589556, -0.78360635147269186, -0.27753148540854827 } }, 0.036350627898137505 }, { { { -0.58653758390109001, -0.75224759589841128, -0.30016198482907891 } }, 0.035000022656145123 }, { { { -0.23286189267848467, -0.81218303420487603, -0.53491500061968278 } }, 0.089553996641201256 }, { { { -0.44976061833542658, -0.77808150709120449, -0.43852543200732902 } }, 0.06312098819082787 }, { { { -0.52867365023543322, -0.73989176486381036, -0.41601003333268127 } }, 0.03681930815868277 }, { { { -0.56903370755061966, -0.71714614572136215, -0.40237053240538195 } }, 0.0349986658044458 }, { { { -0.608434883101728, -0.78746722833958294, -0.098500544750583577 } }, 0.094989309368324934 }, { { { -0.57633874862301404, -0.79873577597403389, -0.17278543635045251 } }, 0.062327979981915728 }, { { { -0.59065446388878895, -0.77711619346836613, -0.21729640157509353 } }, 0.035988306008964265 }, { { { -0.61477770380175345, -0.74823169749510621, -0.24939467069653976 } }, 0.034998759823255947 }, { { { -0.65228028268151761, -0.74785726411410713, 0.12345017349798207 } }, 0.089554102677062389 }, { { { -0.65502316280390716, -0.74660156355641616, -0.11632180141951463 } }, 0.063120915188818058 }, { { { -0.66505386804902744, -0.71897562331200615, -0.20193416173629261 } }, 0.036819344113710482 }, { { { -0.66799220722082098, -0.70196768357699402, -0.24703801793611668 } }, 0.034998707586679619 } };
    FSecondaryMotionWeightedContactAnchor anchors[25]{};
    anchors[0].chainIndex = 4294967295u;
    anchors[0].segmentIndex = 0u;
    anchors[0].definition.direction = { { 0, 0, 0 } };
    anchors[0].definition.offset = { { 0, 0, 0 } };
    anchors[0].definition.cosineLimit = -1;
    anchors[0].constantPosition = { { 0.038167899587724194, 1.2034082212529649, 2.343467225864643 } };
    anchors[0].weight = 0.11424469202756882;
    anchors[1].chainIndex = 0u;
    anchors[1].segmentIndex = 0u;
    anchors[1].definition.direction = { { -0.52493285865791772, -0.8120235580617946, -0.25507495967634691 } };
    anchors[1].definition.offset = { { -0.054212863866343275, -0.044458419311022013, -0.0035370650021264161 } };
    anchors[1].definition.cosineLimit = -1;
    anchors[1].constantPosition = { { 0, 0, 0 } };
    anchors[1].weight = 0.63846737146377563;
    anchors[2].chainIndex = 2u;
    anchors[2].segmentIndex = 0u;
    anchors[2].definition.direction = { { -0.608434883101728, -0.78746722833958294, -0.098500544750583577 } };
    anchors[2].definition.offset = { { -0.0295279162044354, -0.054000883331390637, -0.039942101139859298 } };
    anchors[2].definition.cosineLimit = -1;
    anchors[2].constantPosition = { { 0, 0, 0 } };
    anchors[2].weight = 0.24728794395923615;
    anchors[3].chainIndex = 4294967295u;
    anchors[3].segmentIndex = 0u;
    anchors[3].definition.direction = { { 0, 0, 0 } };
    anchors[3].definition.offset = { { 0, 0, 0 } };
    anchors[3].definition.cosineLimit = -1;
    anchors[3].constantPosition = { { 0.01323480401031378, 1.2120511653011141, 2.3722752948992962 } };
    anchors[3].weight = 0.12004899233579636;
    anchors[4].chainIndex = 0u;
    anchors[4].segmentIndex = 0u;
    anchors[4].definition.direction = { { -0.52493285865791772, -0.8120235580617946, -0.25507495967634691 } };
    anchors[4].definition.offset = { { -0.079146035163626838, -0.035815574788639948, 0.025270970773933588 } };
    anchors[4].definition.cosineLimit = -1;
    anchors[4].constantPosition = { { 0, 0, 0 } };
    anchors[4].weight = 0.16638565063476562;
    anchors[5].chainIndex = 2u;
    anchors[5].segmentIndex = 0u;
    anchors[5].definition.direction = { { -0.608434883101728, -0.78746722833958294, -0.098500544750583577 } };
    anchors[5].definition.offset = { { -0.054461075407562232, -0.045357960471879843, -0.011134107323457987 } };
    anchors[5].definition.cosineLimit = -1;
    anchors[5].constantPosition = { { 0, 0, 0 } };
    anchors[5].weight = 0.71356534957885742;
    anchors[6].chainIndex = 4294967295u;
    anchors[6].segmentIndex = 0u;
    anchors[6].definition.direction = { { 0, 0, 0 } };
    anchors[6].definition.offset = { { 0, 0, 0 } };
    anchors[6].definition.cosineLimit = -1;
    anchors[6].constantPosition = { { 0.024832564150704728, 1.2076333405660802, 2.3591525543784835 } };
    anchors[6].weight = 0.12185923010110855;
    anchors[7].chainIndex = 0u;
    anchors[7].segmentIndex = 0u;
    anchors[7].definition.direction = { { -0.52493285865791772, -0.8120235580617946, -0.25507495967634691 } };
    anchors[7].definition.offset = { { -0.067548239949229316, -0.040233352284013257, 0.012148245053114293 } };
    anchors[7].definition.cosineLimit = -1;
    anchors[7].constantPosition = { { 0, 0, 0 } };
    anchors[7].weight = 0.40765026211738586;
    anchors[8].chainIndex = 2u;
    anchors[8].segmentIndex = 0u;
    anchors[8].definition.direction = { { -0.608434883101728, -0.78746722833958294, -0.098500544750583577 } };
    anchors[8].definition.offset = { { -0.042863284717901184, -0.049775767548779382, -0.024256816573236972 } };
    anchors[8].definition.cosineLimit = -1;
    anchors[8].constantPosition = { { 0, 0, 0 } };
    anchors[8].weight = 0.47049048542976379;
    anchors[9].chainIndex = 4294967295u;
    anchors[9].segmentIndex = 0u;
    anchors[9].definition.direction = { { 0, 0, 0 } };
    anchors[9].definition.offset = { { 0, 0, 0 } };
    anchors[9].definition.cosineLimit = -1;
    anchors[9].constantPosition = { { -0.0070578327001875052, 1.2253731967726798, 2.392513916572335 } };
    anchors[9].weight = 0.14583724737167358;
    anchors[10].chainIndex = 2u;
    anchors[10].segmentIndex = 0u;
    anchors[10].definition.direction = { { -0.608434883101728, -0.78746722833958294, -0.098500544750583577 } };
    anchors[10].definition.offset = { { -0.074753765245418352, -0.032036005671625373, 0.0091045011840639734 } };
    anchors[10].definition.cosineLimit = -1;
    anchors[10].constantPosition = { { 0, 0, 0 } };
    anchors[10].weight = 0.55512070655822754;
    anchors[11].chainIndex = 3u;
    anchors[11].segmentIndex = 0u;
    anchors[11].definition.direction = { { -0.65228028268151761, -0.74785726411410713, 0.12345017349798207 } };
    anchors[11].definition.offset = { { -0.036756537188895572, -0.056468763673514333, -0.013633063095126342 } };
    anchors[11].definition.cosineLimit = -1;
    anchors[11].constantPosition = { { 0, 0, 0 } };
    anchors[11].weight = 0.29904201626777649;
    anchors[12].chainIndex = 4294967295u;
    anchors[12].segmentIndex = 0u;
    anchors[12].definition.direction = { { 0, 0, 0 } };
    anchors[12].definition.offset = { { 0, 0, 0 } };
    anchors[12].definition.cosineLimit = -1;
    anchors[12].constantPosition = { { -0.015925337457352029, 1.2352084389101332, 2.3996044857517487 } };
    anchors[12].weight = 0.16803304851055145;
    anchors[13].chainIndex = 2u;
    anchors[13].segmentIndex = 0u;
    anchors[13].definition.direction = { { -0.608434883101728, -0.78746722833958294, -0.098500544750583577 } };
    anchors[13].definition.offset = { { -0.083621298116039688, -0.022200837441657018, 0.016195089625864911 } };
    anchors[13].definition.cosineLimit = -1;
    anchors[13].constantPosition = { { 0, 0, 0 } };
    anchors[13].weight = 0.2517702579498291;
    anchors[14].chainIndex = 3u;
    anchors[14].segmentIndex = 0u;
    anchors[14].definition.direction = { { -0.65228028268151761, -0.74785726411410713, 0.12345017349798207 } };
    anchors[14].definition.offset = { { -0.045624078221464137, -0.046633560630515436, -0.0065425753046910364 } };
    anchors[14].definition.cosineLimit = -1;
    anchors[14].constantPosition = { { 0, 0, 0 } };
    anchors[14].weight = 0.58019667863845825;
    anchors[15].chainIndex = 0u;
    anchors[15].segmentIndex = 0u;
    anchors[15].definition.direction = { { -0.52493285865791772, -0.8120235580617946, -0.25507495967634691 } };
    anchors[15].definition.offset = { { -0.072573914636570147, -0.086840480955110766, -0.0059952077400374504 } };
    anchors[15].definition.cosineLimit = -1;
    anchors[15].constantPosition = { { 0, 0, 0 } };
    anchors[15].weight = 0.15207086503505707;
    anchors[16].chainIndex = 2u;
    anchors[16].segmentIndex = 0u;
    anchors[16].definition.direction = { { -0.608434883101728, -0.78746722833958294, -0.098500544750583577 } };
    anchors[16].definition.offset = { { -0.047888732266327227, -0.09638251769763112, -0.042400264386150299 } };
    anchors[16].definition.cosineLimit = -1;
    anchors[16].constantPosition = { { 0, 0, 0 } };
    anchors[16].weight = 0.071231454610824585;
    anchors[17].chainIndex = 0u;
    anchors[17].segmentIndex = 1u;
    anchors[17].definition.direction = { { -0.52307487313420853, -0.81364143218233309, -0.25373272735678642 } };
    anchors[17].definition.offset = { { -0.023436341920173732, -0.010829160921236456, 0.017881769463364998 } };
    anchors[17].definition.cosineLimit = -1;
    anchors[17].constantPosition = { { 0, 0, 0 } };
    anchors[17].weight = 0.50396150350570679;
    anchors[18].chainIndex = 2u;
    anchors[18].segmentIndex = 1u;
    anchors[18].definition.direction = { { -0.57633874862301404, -0.79873577597403389, -0.17278543635045251 } };
    anchors[18].definition.offset = { { 0.0099060850463192907, -0.02158157593661969, -0.033043670480284809 } };
    anchors[18].definition.cosineLimit = -1;
    anchors[18].constantPosition = { { 0, 0, 0 } };
    anchors[18].weight = 0.27273622155189514;
    anchors[19].chainIndex = 2u;
    anchors[19].segmentIndex = 0u;
    anchors[19].definition.direction = { { -0.608434883101728, -0.78746722833958294, -0.098500544750583577 } };
    anchors[19].definition.offset = { { -0.072818767057784201, -0.054015966781925856, 0.031359458212148528 } };
    anchors[19].definition.cosineLimit = -1;
    anchors[19].constantPosition = { { 0, 0, 0 } };
    anchors[19].weight = 0.46209827065467834;
    anchors[20].chainIndex = 3u;
    anchors[20].segmentIndex = 0u;
    anchors[20].definition.direction = { { -0.65228028268151761, -0.74785726411410713, 0.12345017349798207 } };
    anchors[20].definition.offset = { { -0.034821381467427602, -0.078448868373265548, 0.0086218857044897262 } };
    anchors[20].definition.cosineLimit = -1;
    anchors[20].constantPosition = { { 0, 0, 0 } };
    anchors[20].weight = 0.43255537748336792;
    anchors[21].chainIndex = 2u;
    anchors[21].segmentIndex = 1u;
    anchors[21].definition.direction = { { -0.57633874862301404, -0.79873577597403389, -0.17278543635045251 } };
    anchors[21].definition.offset = { { -0.015024174470174678, 0.020784853045748009, 0.040715828410524693 } };
    anchors[21].definition.cosineLimit = -1;
    anchors[21].constantPosition = { { 0, 0, 0 } };
    anchors[21].weight = 0.056557107716798782;
    anchors[22].chainIndex = 3u;
    anchors[22].segmentIndex = 1u;
    anchors[22].definition.direction = { { -0.65502316280390716, -0.74660156355641616, -0.11632180141951463 } };
    anchors[22].definition.offset = { { 0.023592879657344714, -0.011475244317131298, -0.0024335004910707309 } };
    anchors[22].definition.cosineLimit = -1;
    anchors[22].constantPosition = { { 0, 0, 0 } };
    anchors[22].weight = 0.048789218068122864;
    anchors[23].chainIndex = 2u;
    anchors[23].segmentIndex = 0u;
    anchors[23].definition.direction = { { -0.608434883101728, -0.78746722833958294, -0.098500544750583577 } };
    anchors[23].definition.offset = { { -0.051240057759298019, -0.066208896155837194, 0.012073879143017385 } };
    anchors[23].definition.cosineLimit = -1;
    anchors[23].constantPosition = { { 0, 0, 0 } };
    anchors[23].weight = 0.91791605949401855;
    anchors[24].chainIndex = 2u;
    anchors[24].segmentIndex = 1u;
    anchors[24].definition.direction = { { -0.57633874862301404, -0.79873577597403389, -0.17278543635045251 } };
    anchors[24].definition.offset = { { 0.006554533433787868, 0.0085919237470165055, 0.021430258087648119 } };
    anchors[24].definition.cosineLimit = -1;
    anchors[24].constantPosition = { { 0, 0, 0 } };
    anchors[24].weight = 0.082083940505981445;
    FSecondaryMotionWeightedContactPoint contacts[8]{};
    contacts[0].firstAnchor = 0u;
    contacts[0].anchorCount = 3u;
    contacts[0].shapes = collisionShapes + 4;
    contacts[0].shapeCount = 8u;
    contacts[1].firstAnchor = 3u;
    contacts[1].anchorCount = 3u;
    contacts[1].shapes = collisionShapes + 4;
    contacts[1].shapeCount = 8u;
    contacts[2].firstAnchor = 6u;
    contacts[2].anchorCount = 3u;
    contacts[2].shapes = collisionShapes + 4;
    contacts[2].shapeCount = 8u;
    contacts[3].firstAnchor = 9u;
    contacts[3].anchorCount = 3u;
    contacts[3].shapes = collisionShapes + 4;
    contacts[3].shapeCount = 8u;
    contacts[4].firstAnchor = 12u;
    contacts[4].anchorCount = 3u;
    contacts[4].shapes = collisionShapes + 4;
    contacts[4].shapeCount = 8u;
    contacts[5].firstAnchor = 15u;
    contacts[5].anchorCount = 4u;
    contacts[5].shapes = collisionShapes + 4;
    contacts[5].shapeCount = 8u;
    contacts[6].firstAnchor = 19u;
    contacts[6].anchorCount = 4u;
    contacts[6].shapes = collisionShapes + 4;
    contacts[6].shapeCount = 8u;
    contacts[7].firstAnchor = 23u;
    contacts[7].anchorCount = 2u;
    contacts[7].shapes = collisionShapes + 4;
    contacts[7].shapeCount = 8u;
    const gk::FVector3d initialPositions[20] = { { { 0.092380806803703308, 1.2478666305541992, 2.3470041751861572 } }, { { 0.1789262592792511, 1.2174925804138184, 2.3283054828643799 } }, { { 0.1325811892747879, 1.1759265661239624, 2.3257684707641602 } }, { { 0.10378836840391159, 1.1540061235427856, 2.3292088508605957 } }, { { 0.082109227776527405, 1.1269700527191162, 2.3243029117584229 } }, { { 0.12187332659959793, 1.2677056789398193, 2.2614600658416748 } }, { { 0.16669756174087524, 1.2562648057937622, 2.1847801208496094 } }, { { 0.15998205542564392, 1.2065303325653076, 2.1464962959289551 } }, { { 0.12596403062343597, 1.1928012371063232, 2.1433424949645996 } }, { { 0.099759355187416077, 1.1699026823043823, 2.1470677852630615 } }, { { 0.067696355283260345, 1.2574095726013184, 2.3834095001220703 } }, { { 0.084980025887489319, 1.3015210628509521, 2.4657406806945801 } }, { { 0.033266671001911163, 1.3189704418182373, 2.4958407878875732 } }, { { 0.042879670858383179, 1.2901930809020996, 2.5151956081390381 } }, { { 0.012216750532388687, 1.2973160743713379, 2.5304913520812988 } }, { { 0.029698986560106277, 1.2818422317504883, 2.4061470031738281 } }, { { -0.04079301655292511, 1.2590452432632446, 2.4564571380615234 } }, { { -0.067072063684463501, 1.2043495178222656, 2.4390778541564941 } }, { { -0.094179831445217133, 1.179436206817627, 2.4394819736480713 } }, { { -0.12105794996023178, 1.1572359800338745, 2.4363796710968018 } } };
    const gk::FVector3d witnessPositions[20] = { { { 0.092380806803703308, 1.2478666305541992, 2.3470041751861572 } }, { { 0.18389024000065016, 1.2307864285791443, 2.3371737687733125 } }, { { 0.14774280953086699, 1.1809429413032912, 2.3276332390757295 } }, { { 0.11752051637099481, 1.1618891401171658, 2.3343365073164613 } }, { { 0.095841383853080081, 1.1348530794068212, 2.3294305700494427 } }, { { 0.12187332659959793, 1.2677056789398193, 2.2614600658416748 } }, { { 0.16381252698648996, 1.2612784475546428, 2.1825949120534407 } }, { { 0.15880726514596649, 1.216429384651684, 2.1384613866920916 } }, { { 0.1249216064009333, 1.2032159033497893, 2.1327318582137891 } }, { { 0.098435168602370476, 1.1803746642985466, 2.1340175562491508 } }, { { 0.067696355283260345, 1.2574095726013184, 2.3834095001220703 } }, { { 0.092148220764872488, 1.3062686879863223, 2.4611131809392321 } }, { { 0.068848208525893503, 1.3440620882375367, 2.5048572710056747 } }, { { 0.082310259856041693, 1.3288093462264361, 2.5345437191684663 } }, { { 0.052620670480232723, 1.3377478396371163, 2.5507777823508317 } }, { { 0.029698986560106277, 1.2818422317504883, 2.4061470031738281 } }, { { -0.040793027306560509, 1.2590452397855381, 2.4564571457363917 } }, { { -0.067072073533051599, 1.2043495162282736, 2.4390778624299032 } }, { { -0.094179807802835538, 1.1794362360034081, 2.4394819814222006 } }, { { -0.12105794746495821, 1.1572359917530091, 2.436379676430108 } } };
    gk::String error;
    gk::Array<gk::FVector3d> witness;
    for (uint32_t i = 0; i < 20u; ++i)
        if (!witness.Append(witnessPositions[i]))
        {
            return Fail("could not allocate the captured clear witness");
        }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 4u, references, 16u, anchors, 25u, contacts, 8u, witness, error, 32u))
        return Fail(error.Empty() ? "the solver rejected the known clear witness" : error.CStr());
    if (memcmp(witnessPositions, witness.Data(), sizeof(witnessPositions)) != 0 || !HasCapturedFixtureConstraints(chains, 4u, references, anchors, contacts, 8u, initialPositions, witness, error))
        return Fail("the solver changed or invalidated the known clear witness");
    gk::Array<gk::FVector3d> positions;
    for (uint32_t i = 0; i < 20u; ++i)
        if (!positions.Append(initialPositions[i]))
        {
            return Fail("could not allocate the captured failing input");
        }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 4u, references, 16u, anchors, 25u, contacts, 8u, positions, error, 32u))
    {
        fprintf(stderr, "captured weighted-contact cluster failed: %s\n", error.CStr());
        return Fail(error.Empty() ? "the solver failed to resolve a captured feasible weighted-contact cluster" : error.CStr());
    }
    if (!HasCapturedFixtureConstraints(chains, 4u, references, anchors, contacts, 8u, initialPositions, positions, error))
        return Fail(error.Empty() ? "the captured solve violated a stored root, length, cone, body, or weighted constraint" : error.CStr());

    // 無関係な鎖を追加して連立方向数の上限を超え、姿勢を変えずに戻ることを確かめる。
    // 上限を超える追加鎖の数。
    const uint32_t unrelatedChainCount = 256u;
    // 元の接触鎖と追加鎖を含む入力範囲。
    FSecondaryMotionContactChain expandedChains[4u + unrelatedChainCount]{};
    // 元の基準線分と追加鎖の基準方向。
    FSecondaryMotionReferenceSegment expandedReferences[16u + unrelatedChainCount]{};
    // 拡張後の姿勢と呼び出し前の比較用姿勢。
    gk::Array<gk::FVector3d> expandedPositions;
    gk::Array<gk::FVector3d> expandedSnapshot;
    for (uint32_t i = 0; i < 4u; ++i)
        expandedChains[i] = chains[i];
    for (uint32_t i = 0; i < 16u; ++i)
        expandedReferences[i] = references[i];
    for (uint32_t i = 0; i < 20u; ++i)
        if (!expandedPositions.Append(initialPositions[i]))
            return Fail("could not copy the captured positions for the direction-cap case");
    for (uint32_t i = 0; i < unrelatedChainCount; ++i)
    {
        // 追加鎖の配列位置と整数root座標。
        const uint32_t chainIndex = 4u + i;
        const uint32_t segmentIndex = 16u + i;
        const uint32_t rootPoint = 20u + i * 2u;
        const double rootX = static_cast<double>(10u + i * 2u);
        expandedChains[chainIndex].firstPoint = rootPoint;
        expandedChains[chainIndex].firstSegment = segmentIndex;
        expandedChains[chainIndex].segmentCount = 1u;
        expandedChains[chainIndex].cosineLimit = 1.0;
        expandedReferences[segmentIndex] = { { { 1.0, 0.0, 0.0 } }, 1.0 };
        const gk::FVector3d unrelatedRoot = { { rootX, 0.0, 0.0 } };
        const gk::FVector3d unrelatedChild = { { rootX + 1.0, 0.0, 0.0 } };
        if (!expandedPositions.Append(unrelatedRoot) || !expandedPositions.Append(unrelatedChild))
            return Fail("could not append unrelated chains for the direction-cap case");
    }
    for (uint32_t i = 0; i < expandedPositions.Count(); ++i)
        if (!expandedSnapshot.Append(expandedPositions.At(i)))
            return Fail("could not snapshot the direction-cap input positions");
    // 元の16方向と追加256方向で、合計272方向を構成する。
    if (expandedChains[0].segmentCount + expandedChains[1].segmentCount + expandedChains[2].segmentCount + expandedChains[3].segmentCount + unrelatedChainCount != 272u)
        return Fail("the direction-cap fixture does not exceed the coupled solve limit");
    if (ResolvePreparedSecondaryMotionWeightedContacts(expandedChains, 4u + unrelatedChainCount, expandedReferences, 16u + unrelatedChainCount, anchors, 25u, contacts, 8u, expandedPositions, error, 32u) || !error.Empty())
        return Fail(error.Empty() ? "the direction-cap case unexpectedly resolved the captured failing input" : error.CStr());
    if (expandedPositions.Count() != expandedSnapshot.Count() || memcmp(expandedSnapshot.Data(), expandedPositions.Data(), sizeof(gk::FVector3d) * expandedSnapshot.Count()) != 0)
        return Fail("the direction-cap case changed positions before returning unresolved");
    return true;
}

bool TestOpposedAnchorsUseTheirWeightedSkinPosition(float bodyRadius)
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, bodyRadius };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error) || prepared.Count() != 1u)
    {
        return Fail("could not prepare the independent body sphere fixture");
    }

    const gk::FVector3d roots[2] = { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } } };
    const gk::FVector3d initialChildren[2] = { { { 1.0, 0.0, 0.0 } }, { { -1.0, 0.0, 0.0 } } };
    const gk::FVector3d initialBlend = WeightedPosition(initialChildren[0], initialChildren[1], 0.5, 0.5);
    if (Distance(initialBlend, roots[0]) >= sourceBody.radius || Distance(initialChildren[0], roots[0]) <= sourceBody.radius || Distance(initialChildren[1], roots[1]) <= sourceBody.radius)
    {
        return Fail("the independent fixture must start with only the blended point inside the body sphere");
    }

    FSecondaryMotionContactChain chains[2]{};
    chains[0].firstPoint = 0;
    chains[0].firstSegment = 0;
    chains[0].segmentCount = 1;
    chains[0].cosineLimit = 0.0;
    chains[0].shapes = prepared.Data();
    chains[0].shapeCount = prepared.Count();
    chains[1].firstPoint = 2;
    chains[1].firstSegment = 1;
    chains[1].segmentCount = 1;
    chains[1].cosineLimit = 0.0;
    chains[1].shapes = prepared.Data();
    chains[1].shapeCount = prepared.Count();

    const FSecondaryMotionReferenceSegment references[2] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { -1.0, 0.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    anchors[0].chainIndex = 0;
    anchors[0].segmentIndex = 0;
    anchors[0].definition.direction = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.offset = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.5;
    anchors[1].chainIndex = 1;
    anchors[1].segmentIndex = 0;
    anchors[1].definition.direction = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.offset = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.5;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.firstAnchor = 0;
    contact.anchorCount = 2;
    contact.shapes = prepared.Data();
    contact.shapeCount = prepared.Count();

    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(roots[0]) || !positions.Append(initialChildren[0]) || !positions.Append(roots[1]) || !positions.Append(initialChildren[1]))
    {
        return Fail("could not allocate the weighted contact positions");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 2, &contact, 1, positions, error))
    {
        fprintf(stderr, "weighted blend radius %.6f failed: %s\n", bodyRadius, error.CStr());
        return Fail(error.Empty() ? "the solver rejected a feasible weighted blend fixture" : error.CStr());
    }
    if (!IsSolved(positions, anchors, 2, prepared.At(0), roots[0], roots[1]))
    {
        return Fail("the solver did not clear the blended point while preserving roots, lengths, and allowed angles");
    }
    const FSecondaryMotionWeightedContactAnchor reversedAnchors[2] = { anchors[1], anchors[0] };
    gk::Array<gk::FVector3d> reversedPositions;
    if (!reversedPositions.Append(roots[0]) || !reversedPositions.Append(initialChildren[0]) || !reversedPositions.Append(roots[1]) || !reversedPositions.Append(initialChildren[1]) || !ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, reversedAnchors, 2, &contact, 1, reversedPositions, error) || !IsSolved(reversedPositions, reversedAnchors, 2, prepared.At(0), roots[0], roots[1]))
    {
        return Fail(error.Empty() ? "reversing anchor order changed a feasible blend into an invalid result" : error.CStr());
    }
    return true;
}

bool TestUnrelatedChainKeepsItsCurrentDirectionDuringWeightedSeeding()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.99f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error))
    {
        return Fail("could not prepare the independent weighted body sphere");
    }
    const gk::FVector3d roots[3] = { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, { { 10.0, 0.0, 0.0 } } };
    const gk::FVector3d initialChildren[3] = { { { 1.0, 0.0, 0.0 } }, { { -1.0, 0.0, 0.0 } }, { { 10.6, 0.8, 0.0 } } };
    FSecondaryMotionContactChain chains[3]{};
    for (uint32_t index = 0; index < 3u; ++index)
    {
        chains[index].firstPoint = index * 2u;
        chains[index].firstSegment = index;
        chains[index].segmentCount = 1;
        chains[index].cosineLimit = 0.0;
    }
    chains[0].shapes = prepared.Data();
    chains[0].shapeCount = prepared.Count();
    chains[1].shapes = prepared.Data();
    chains[1].shapeCount = prepared.Count();
    const FSecondaryMotionReferenceSegment references[3] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { -1.0, 0.0, 0.0 } }, 1.0 }, { { { 0.0, 1.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    anchors[0].chainIndex = 0;
    anchors[0].segmentIndex = 0;
    anchors[0].definition.direction = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.offset = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.5;
    anchors[1].chainIndex = 1;
    anchors[1].segmentIndex = 0;
    anchors[1].definition.direction = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.offset = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.5;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.anchorCount = 2;
    contact.shapes = prepared.Data();
    contact.shapeCount = prepared.Count();
    gk::Array<gk::FVector3d> positions;
    for (uint32_t index = 0; index < 3u; ++index)
    {
        if (!positions.Append(roots[index]) || !positions.Append(initialChildren[index]))
        {
            return Fail("could not allocate the three-chain weighted fixture");
        }
    }
    if (fabs(Distance(roots[2], initialChildren[2]) - 1.0) > 1.0e-12 || (initialChildren[2].value[1] - roots[2].value[1]) < 0.0)
    {
        return Fail("the unrelated chain must begin at a valid non-reference direction");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 3, references, 3, anchors, 2, &contact, 1, positions, error))
    {
        fprintf(stderr, "unrelated-chain weighted solve failed: %s\n", error.CStr());
        return Fail(error.Empty() ? "the solver rejected a feasible weighted fixture with an unrelated chain" : error.CStr());
    }
    if (positions.Count() != 6u || Distance(positions.At(4), roots[2]) > 1.0e-12 || Distance(positions.At(5), initialChildren[2]) > 1.0e-12 || fabs(Distance(positions.At(4), positions.At(5)) - 1.0) > 1.0e-12)
    {
        return Fail("weighted-contact seed generation changed a chain unrelated to all contacts");
    }
    const gk::FVector3d solvedFirstDirection = { { positions.At(1).value[0] - positions.At(0).value[0], positions.At(1).value[1] - positions.At(0).value[1], positions.At(1).value[2] - positions.At(0).value[2] } };
    const gk::FVector3d solvedSecondDirection = { { positions.At(3).value[0] - positions.At(2).value[0], positions.At(3).value[1] - positions.At(2).value[1], positions.At(3).value[2] - positions.At(2).value[2] } };
    const gk::FVector3d solvedWeighted = WeightedPosition(EvaluateAnchorPoint(anchors[0], positions), EvaluateAnchorPoint(anchors[1], positions), 0.5, 0.5);
    const gk::FVector3d bodyCenter = { { 0.0, 0.0, 0.0 } };
    if (Distance(positions.At(0), roots[0]) > 1.0e-12 || Distance(positions.At(2), roots[1]) > 1.0e-12 || fabs(Distance(positions.At(0), positions.At(1)) - 1.0) > 0.0011 || fabs(Distance(positions.At(2), positions.At(3)) - 1.0) > 0.0011 || solvedFirstDirection.value[0] < -0.001 || solvedSecondDirection.value[0] > 0.001 || Distance(solvedWeighted, bodyCenter) < prepared.At(0).radius * 0.998)
    {
        return Fail("the contact chains lost root, length, cone, or weighted-body clearance");
    }
    return true;
}

bool TestTinyBodyContactDoesNotOvercorrect()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 1.0f, 0.0009989f, 0.0f }, { 1.0f, 0.0009989f, 0.0f }, 0.001f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error) || prepared.Count() != 1u)
    {
        return Fail("could not prepare the tiny body-contact sphere");
    }
    FSecondaryMotionContactChain chain{};
    chain.firstPoint = 0;
    chain.firstSegment = 0;
    chain.segmentCount = 1;
    chain.cosineLimit = 0.0;
    chain.shapes = prepared.Data();
    chain.shapeCount = prepared.Count();
    const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d initialChild = { { 1.0, 0.0, 0.0 } };
    const gk::FVector3d sphereCenter = { { sourceBody.start.x, sourceBody.start.y, sourceBody.start.z } };
    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(root) || !positions.Append(initialChild))
    {
        return Fail("could not allocate the tiny body-contact points");
    }
    if (Distance(initialChild, sphereCenter) >= sourceBody.radius * 0.999)
    {
        return Fail("the tiny contact fixture must begin just inside the solver's clearance threshold");
    }
    const double witnessY = -0.0000002;
    const double witnessX = sqrt(1.0 - witnessY * witnessY);
    const gk::FVector3d clearWitness = { { witnessX, witnessY, 0.0 } };
    if (fabs(Distance(root, clearWitness) - 1.0) > 1.0e-12 || Distance(clearWitness, sphereCenter) < sourceBody.radius * 0.999 || witnessY > 0.0)
    {
        return Fail("the independent tiny-angle witness does not preserve length and clear the body sphere");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, nullptr, 0, nullptr, 0, positions, error))
    {
        fprintf(stderr, "tiny body-contact solve failed: %s\n", error.CStr());
        return Fail(error.Empty() ? "the solver rejected a feasible tiny body-contact witness" : error.CStr());
    }
    const gk::FVector3d& solvedChild = positions.At(1);
    const double solvedDisplacement = Distance(initialChild, solvedChild);
    if (positions.Count() != 2u || Distance(positions.At(0), root) > 1.0e-12 || fabs(Distance(root, solvedChild) - 1.0) > 0.0011 || (solvedChild.value[0] - root.value[0]) < 0.0 || Distance(solvedChild, sphereCenter) < sourceBody.radius * 0.999 || solvedDisplacement >= 0.001)
    {
        return Fail("the tiny body contact either remained penetrating or moved the chain far beyond the nearby clear solution");
    }
    return true;
}

bool TestRotatedCurrentPoseKeepsNearbyClearCorrection()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.9999f, 0.0f }, { 0.0f, 0.9999f, 0.0f }, 0.001f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error))
    {
        return Fail("could not prepare the rotated-pose contact sphere");
    }
    FSecondaryMotionContactChain chain{};
    chain.firstPoint = 0;
    chain.firstSegment = 0;
    chain.segmentCount = 1;
    chain.cosineLimit = 0.0;
    chain.shapes = prepared.Data();
    chain.shapeCount = prepared.Count();
    const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d initialChild = { { 0.0, 1.0, 0.0 } };
    gk::Array<gk::FVector3d> positions;
    if (!positions.Append(root) || !positions.Append(initialChild))
    {
        return Fail("could not allocate the rotated-pose contact points");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, nullptr, 0, nullptr, 0, positions, error))
    {
        return Fail(error.Empty() ? "the solver rejected a nearby clear rotated-pose correction" : error.CStr());
    }
    const gk::FVector3d& solvedChild = positions.At(1);
    if (Distance(positions.At(0), root) > 1.0e-12 || fabs(Distance(root, solvedChild) - 1.0) > 0.0011 || Distance(solvedChild, initialChild) >= 0.003 || Distance(solvedChild, { { sourceBody.start.x, sourceBody.start.y, sourceBody.start.z } }) < sourceBody.radius * 0.999)
    {
        return Fail("the nearby rotated-pose contact was cleared by a large pose jump");
    }
    return true;
}

bool TestBodyDirectionPrefersNearbyClearPoseToClearReference()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.9999f, 0.0f }, { 0.0f, 0.9999f, 0.0f }, 0.001f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error))
    {
        return Fail("could not prepare the body-direction local-clearance sphere");
    }
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d reference = { { 1.0, 0.0, 0.0 } };
    const gk::FVector3d initial = { { 0.0, 1.0, 0.0 } };
    gk::FVector3d direction = initial;
    if (!FindPreparedSecondaryMotionCollisionDirection(prepared.Data(), prepared.Count(), root, 1.0, false, reference, 0.0, direction, error, 0.5))
    {
        return Fail(error.Empty() ? "the body-direction solver rejected a nearby clear witness" : error.CStr());
    }
    const gk::FVector3d endpoint = { { direction.value[0], direction.value[1], direction.value[2] } };
    const gk::FVector3d bodyCenter = { { sourceBody.start.x, sourceBody.start.y, sourceBody.start.z } };
    if (Distance(direction, initial) >= 0.003 || fabs(Distance(root, endpoint) - 1.0) > 1.0e-12 || Distance(endpoint, bodyCenter) < sourceBody.radius * 0.999)
    {
        return Fail("the body-direction solver snapped from a nearby clear pose to the distant reference");
    }
    return true;
}

bool TestThreeAnchorsAndInvalidInputsAreAtomic()
{
    const gk::model::animation::FModelSecondaryMotionCollisionShape sourceBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.4f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sourceBody, 1, prepared, error))
    {
        return Fail("could not prepare the three-anchor body sphere");
    }
    FSecondaryMotionContactChain chains[2]{};
    chains[0].firstPoint = 0;
    chains[0].firstSegment = 0;
    chains[0].segmentCount = 1;
    chains[0].cosineLimit = 0.0;
    chains[0].shapes = prepared.Data();
    chains[0].shapeCount = 1;
    chains[1].firstPoint = 2;
    chains[1].firstSegment = 1;
    chains[1].segmentCount = 1;
    chains[1].cosineLimit = 0.0;
    chains[1].shapes = prepared.Data();
    chains[1].shapeCount = 1;
    const FSecondaryMotionReferenceSegment references[2] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { -1.0, 0.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchors[3]{};
    anchors[0].chainIndex = 0;
    anchors[0].definition.direction = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.offset = { { 1.0, 0.0, 0.0 } };
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.4;
    anchors[1].chainIndex = 1;
    anchors[1].definition.direction = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.offset = { { -1.0, 0.0, 0.0 } };
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.4;
    anchors[2].chainIndex = 0xFFFFFFFFu;
    anchors[2].constantPosition = { { 0.0, 0.0, 0.0 } };
    anchors[2].weight = 0.2;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.anchorCount = 3;
    contact.shapes = prepared.Data();
    contact.shapeCount = 1;
    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d initial[4] = { { { 0.0, 0.0, 0.0 } }, { { 1.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, { { -1.0, 0.0, 0.0 } } };
    if (!positions.AppendRange(initial, 4))
    {
        return Fail("could not allocate the three-anchor positions");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error))
    {
        return Fail(error.Empty() ? "the solver rejected a feasible fixed-plus-moving three-anchor point" : error.CStr());
    }
    const gk::FVector3d firstRoot = initial[0];
    const gk::FVector3d secondRoot = initial[2];
    if (!IsSolved(positions, anchors, 3, prepared.At(0), firstRoot, secondRoot))
    {
        return Fail("the solver did not clear a three-anchor blend with a fixed contribution");
    }

    gk::FVector3d snapshot[4];
    memcpy(snapshot, positions.Data(), sizeof(snapshot));
    if (ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error, 0) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("zero weighted-contact iterations were accepted or changed the pose");
    }
    if (ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error, 33) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("more than 32 weighted-contact iterations were accepted or changed the pose");
    }
    anchors[2].weight = 0.0;
    if (ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("a zero influence weight was accepted or changed the pose on rejection");
    }
    anchors[2].weight = 0.2;
    anchors[1].definition.direction.value[0] = NAN;
    if (ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("a non-finite moving anchor was accepted or changed the pose on rejection");
    }
    anchors[1].definition.direction.value[0] = -1.0;
    gk::SetAllocationFailureAfterForTesting(0);
    const bool allocationResult = ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error);
    gk::ResetAllocationFailureForTesting();
    if (allocationResult || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("allocation failure did not preserve the weighted contact pose");
    }
    return true;
}

bool TestAntiparallelInitialDirectionFindsClearSeed()
{
    using namespace gk::model::animation;
    const FModelSecondaryMotionCollisionShape body = { { -2.0f, 0.0f, 0.0f }, { -2.0f, 0.0f, 0.0f }, 0.8f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&body, 1, prepared, error))
    {
        return Fail("could not prepare the antiparallel weighted-contact body");
    }

    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 1;
    chain.cosineLimit = -1.0;
    chain.shapes = prepared.Data();
    chain.shapeCount = 1;
    const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0;
    anchor.definition.direction = { { 1.0, 0.0, 0.0 } };
    anchor.definition.offset = { { 2.0, 0.0, 0.0 } };
    anchor.definition.cosineLimit = -1.0;
    anchor.weight = 1.0;
    const FSecondaryMotionWeightedContactPoint contact = { 0, 1, prepared.Data(), 1 };
    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d antiparallelChild = { { -1.0, 0.0, 0.0 } };
    const gk::FVector3d feasibleChild = { { 0.0, 1.0, 0.0 } };
    const gk::FVector3d initialAnchor = { { -2.0, 0.0, 0.0 } };
    const gk::FVector3d feasibleAnchor = { { 0.0, 2.0, 0.0 } };
    const gk::FVector3d bodyCenter = { { -2.0, 0.0, 0.0 } };
    if (!positions.Append(root) || !positions.Append(antiparallelChild) || Distance(initialAnchor, bodyCenter) >= prepared.At(0).radius || Distance(feasibleAnchor, bodyCenter) <= prepared.At(0).radius)
    {
        return Fail("the independent antiparallel input or alternate clear witness is invalid");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, &anchor, 1, &contact, 1, positions, error))
    {
        return Fail(error.Empty() ? "the solver did not recover from an antiparallel initial direction" : error.CStr());
    }
    const double length = Distance(positions.At(0), positions.At(1));
    const gk::FVector3d actualDirection = { { positions.At(1).value[0] / length, positions.At(1).value[1] / length, positions.At(1).value[2] / length } };
    const gk::FVector3d actualAnchor = { { actualDirection.value[0] * 2.0, actualDirection.value[1] * 2.0, actualDirection.value[2] * 2.0 } };
    if (Distance(positions.At(0), root) > 1.0e-12 || fabs(length - 1.0) > 0.0011 || Distance(actualAnchor, bodyCenter) < prepared.At(0).radius * 0.998)
    {
        return Fail("the antiparallel solve changed its root, bone length, or weighted contact clearance");
    }
    return true;
}

/**
 * 反平行に近い接触で従来seed探索が有効解を見つけることを確かめる。
 */
bool TestNearAntiparallelWeightedContactUsesSeedFallback()
{
    using namespace gk::model::animation;
    // 反平行に近い姿勢で動かす衝突形状を用意する。
    const FModelSecondaryMotionCollisionShape body = { { -1.0f, 0.0625f, 0.0f }, { -1.0f, 0.0625f, 0.0f }, 1.5f };
    // 準備済み形状と失敗理由を保持する。
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&body, 1, prepared, error))
    {
        return Fail("could not prepare the coupled-budget fallback body");
    }

    // 連立方向上限を超える鎖数と、全鎖の入力配列を保持する。
    const uint32_t unrelatedChainCount = 256u;
    FSecondaryMotionContactChain chains[257]{};
    FSecondaryMotionReferenceSegment references[257]{};
    chains[0].segmentCount = 1;
    chains[0].cosineLimit = -1.0;
    chains[0].shapes = prepared.Data();
    chains[0].shapeCount = 1;
    // 動く鎖の基準方向を反平行姿勢から少しだけずらす。
    const gk::FVector3d referenceDirection = { { sqrt(255.0) / 16.0, 1.0 / 16.0, 0.0 } };
    references[0] = { { { referenceDirection.value[0], referenceDirection.value[1], referenceDirection.value[2] } }, 1.0 };
    // 接触に関係しない鎖を、整数座標のまま配列へ追加する。
    for (uint32_t chainIndex = 0; chainIndex < unrelatedChainCount; ++chainIndex)
    {
        FSecondaryMotionContactChain& chain = chains[chainIndex + 1u];
        chain.firstPoint = 2u + chainIndex * 2u;
        chain.firstSegment = chainIndex + 1u;
        chain.segmentCount = 1;
        chain.cosineLimit = 1.0;
        references[chainIndex + 1u] = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    }

    // 動く鎖の重み付き接触点を構成する。
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0;
    anchor.definition.direction = referenceDirection;
    anchor.definition.offset = { { 2.0, 0.0, 0.0 } };
    anchor.definition.cosineLimit = -1.0;
    anchor.weight = 1.0;
    const FSecondaryMotionWeightedContactPoint contact = { 0, 1, prepared.Data(), 1 };
    // 初期姿勢と固定rootを保持する位置配列を作る。
    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d antiparallelChild = { { -1.0, 0.0, 0.0 } };
    const gk::FVector3d bodyCenter = { { -1.0, 0.0625, 0.0 } };
    if (!positions.Append(root) || !positions.Append(antiparallelChild))
    {
        return Fail("could not initialize the coupled-budget fallback pose");
    }
    // 各無関係鎖のrootとchildを整数座標で追加する。
    for (uint32_t chainIndex = 0; chainIndex < unrelatedChainCount; ++chainIndex)
    {
        const double rootX = static_cast<double>(10u + chainIndex * 2u);
        const gk::FVector3d unrelatedRoot = { { rootX, 0.0, 0.0 } };
        const gk::FVector3d unrelatedChild = { { rootX + 1.0, 0.0, 0.0 } };
        if (!positions.Append(unrelatedRoot) || !positions.Append(unrelatedChild))
        {
            return Fail("could not append unrelated chains for the coupled-budget fallback");
        }
    }

    // 接触点の侵入と回避可能性を独立に確かめる姿勢を作る。
    gk::Array<gk::FVector3d> feasiblePositions;
    const gk::FVector3d feasibleChild = { { 0.0, 1.0, 0.0 } };
    if (!feasiblePositions.Append(root) || !feasiblePositions.Append(feasibleChild))
    {
        return Fail("could not build the coupled-budget fallback witness pose");
    }
    const gk::FVector3d initialAnchor = EvaluateCapturedFixtureAnchorPoint(anchor, chains, positions);
    const gk::FVector3d alternateAnchor = EvaluateCapturedFixtureAnchorPoint(anchor, chains, feasiblePositions);
    if (Distance(initialAnchor, bodyCenter) >= prepared.At(0).radius || Distance(alternateAnchor, bodyCenter) <= prepared.At(0).radius)
    {
        return Fail("the coupled-budget fallback input lacks a valid independent witness");
    }
    // 一回の局所反復後に上限超過を経て従来seed探索へ戻す。
    const bool resolved = ResolvePreparedSecondaryMotionWeightedContacts(chains, unrelatedChainCount + 1u, references, unrelatedChainCount + 1u, &anchor, 1, &contact, 1, positions, error, 1u);
    if (!resolved || !error.Empty() || positions.Count() != 2u + unrelatedChainCount * 2u)
    {
        return Fail(error.Empty() ? "the solver did not fall back when its coupled-direction budget was exceeded" : error.CStr());
    }
    if (Distance(positions.At(0), root) > 1.0e-12)
    {
        return Fail("the coupled-budget fallback moved the constrained root");
    }
    // 出力されたactive鎖の長さと方向を独立に検査する。
    const double length = Distance(positions.At(0), positions.At(1));
    const gk::FVector3d actualDirection = { { positions.At(1).value[0] / length, positions.At(1).value[1] / length, positions.At(1).value[2] / length } };
    const gk::FVector3d actualAnchor = EvaluateCapturedFixtureAnchorPoint(anchor, chains, positions);
    if (fabs(length - 1.0) > 0.0011)
    {
        return Fail("the coupled-budget fallback changed the active chain length");
    }
    // 保存後の鎖本体が衝突形状を離れたことを検査する。
    gk::Array<gk::FVector3d> bodyGradients;
    if (!bodyGradients.Append(gk::FVector3d{}))
    {
        return Fail("could not allocate the coupled-budget body-check workspace");
    }
    double bodyMerit = 0.0;
    if (!EvaluatePreparedSecondaryMotionChainContactsMerit(chains[0].shapes, chains[0].shapeCount, references, 1u, chains[0].cosineLimit, 1.0, nullptr, 0u, positions.Data(), 2u, bodyMerit, bodyGradients.Data(), 1u, error) || bodyMerit != 0.0)
    {
        return Fail("the coupled-budget fallback left its chain body uncleared");
    }
    // 保存後のweighted接触が形状から離れたことを判定する。
    const double directionDot = actualDirection.value[0] * references[0].direction.value[0] + actualDirection.value[1] * references[0].direction.value[1] + actualDirection.value[2] * references[0].direction.value[2];
    if (directionDot < chains[0].cosineLimit - 0.001)
    {
        return Fail("the coupled-budget fallback violated the active chain cone");
    }
    double contactPenetration = 0.0;
    double contactSpanAmount = 0.0;
    gk::FVector3d contactNormal = {};
    if (!QueryPreparedSecondaryMotionFreeSegmentContact(contact.shapes[0], actualAnchor, actualAnchor, contactPenetration, contactSpanAmount, contactNormal, error, 0.5) || contactPenetration > 0.0)
    {
        return Fail("the coupled-budget fallback left its weighted contact uncleared");
    }
    // 上限超過時にも関係しない鎖の全座標が保たれたことを確かめる。
    for (uint32_t chainIndex = 0; chainIndex < unrelatedChainCount; ++chainIndex)
    {
        const uint32_t pointIndex = 2u + chainIndex * 2u;
        const double rootX = static_cast<double>(10u + chainIndex * 2u);
        if (positions.At(pointIndex).value[0] != rootX || positions.At(pointIndex).value[1] != 0.0 || positions.At(pointIndex).value[2] != 0.0 || positions.At(pointIndex + 1u).value[0] != rootX + 1.0 || positions.At(pointIndex + 1u).value[1] != 0.0 || positions.At(pointIndex + 1u).value[2] != 0.0)
        {
            return Fail("the coupled-budget fallback changed an unrelated chain");
        }
    }
    return true;
}

bool TestRootZeroOffsetAndFixedOnlyContacts()
{
    using namespace gk::model::animation;
    const FModelSecondaryMotionCollisionShape nearBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.4f };
    const FModelSecondaryMotionCollisionShape farBody = { { 2.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, 0.4f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> nearPrepared;
    gk::Array<FPreparedSecondaryMotionCollisionShape> farPrepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&nearBody, 1, nearPrepared, error) || !PrepareSecondaryMotionCollisionShapes(&farBody, 1, farPrepared, error))
    {
        return Fail("could not prepare fixed-root weighted-contact shapes");
    }

    const FSecondaryMotionReferenceSegment reference = { { { 0.0, 1.0, 0.0 } }, 1.0 };
    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 1;
    chain.cosineLimit = 0.0;
    chain.shapes = nearPrepared.Data();
    chain.shapeCount = 1;
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0;
    anchor.definition.direction = { { 0.0, 1.0, 0.0 } };
    anchor.definition.offset = {};
    anchor.definition.cosineLimit = 0.0;
    anchor.weight = 1.0;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.anchorCount = 1;
    contact.shapes = nearPrepared.Data();
    contact.shapeCount = 1;
    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d initial[2] = { { { 0.0, 0.0, 0.0 } }, { { 0.0, 1.0, 0.0 } } };
    if (!positions.AppendRange(initial, 2))
    {
        return Fail("could not allocate root zero-offset positions");
    }
    gk::FVector3d snapshot[2];
    memcpy(snapshot, positions.Data(), sizeof(snapshot));
    if (ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, &anchor, 1, &contact, 1, positions, error) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("a zero-offset root anchor was moved out of an impossible body contact");
    }

    chain.shapes = farPrepared.Data();
    contact.shapes = farPrepared.Data();
    contact.shapeCount = 1;
    memcpy(positions.Data(), snapshot, sizeof(snapshot));
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, &reference, 1, &anchor, 1, &contact, 1, positions, error) || memcmp(snapshot, positions.Data(), sizeof(snapshot)) != 0)
    {
        return Fail("a clear zero-offset root anchor did not remain a fixed point");
    }

    FSecondaryMotionWeightedContactAnchor fixedAnchor{};
    fixedAnchor.chainIndex = 0xFFFFFFFFu;
    fixedAnchor.constantPosition = { { 1.0, 0.0, 0.0 } };
    fixedAnchor.weight = 1.0;
    FSecondaryMotionWeightedContactPoint fixedContact = { 0, 1, nearPrepared.Data(), 1 };
    gk::Array<gk::FVector3d> noPositions;
    if (!ResolvePreparedSecondaryMotionWeightedContacts(nullptr, 0, nullptr, 0, &fixedAnchor, 1, &fixedContact, 1, noPositions, error) || noPositions.Count() != 0)
    {
        return Fail("a purely fixed contact outside the body was not accepted without chains");
    }
    fixedAnchor.constantPosition = {};
    if (ResolvePreparedSecondaryMotionWeightedContacts(nullptr, 0, nullptr, 0, &fixedAnchor, 1, &fixedContact, 1, noPositions, error) || noPositions.Count() != 0)
    {
        return Fail("an impossible purely fixed contact was accepted without chains");
    }
    return true;
}

/**
 * 先頭の骨の回転が主な混合点を、固定した腰の寄与を保ったまま身体から離す。
 */
bool TestRootHeavyWeightedOffsetsWithFixedHip()
{
    using namespace gk::model::animation;
    const FModelSecondaryMotionCollisionShape body = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.08f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&body, 1, prepared, error))
    {
        return Fail("could not prepare the root-heavy weighted body sphere");
    }

    const gk::FVector3d roots[2] = { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } } };
    const gk::FVector3d initial[4] = { roots[0], { { 0.0, 1.0, 0.0 } }, roots[1], { { 0.0, -1.0, 0.0 } } };
    FSecondaryMotionContactChain chains[2]{};
    for (uint32_t index = 0; index < 2; ++index)
    {
        chains[index].firstPoint = index * 2u;
        chains[index].firstSegment = index;
        chains[index].segmentCount = 1;
        chains[index].cosineLimit = 0.0;
    }
    FSecondaryMotionReferenceSegment references[2] = { { { { 0.0, 1.0, 0.0 } }, 1.0 }, { { { 0.0, -1.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchors[3]{};
    anchors[0].chainIndex = 0;
    anchors[0].segmentIndex = 0;
    anchors[0].definition.direction = references[0].direction;
    anchors[0].definition.offset = { { 0.2, 0.0, 0.0 } };
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.88;
    anchors[1].chainIndex = 1;
    anchors[1].segmentIndex = 0;
    anchors[1].definition.direction = references[1].direction;
    anchors[1].definition.offset = { { 0.2, 0.0, 0.0 } };
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.003;
    anchors[2].chainIndex = 0xFFFFFFFFu;
    anchors[2].constantPosition = { { -1.5, 0.0, 0.0 } };
    anchors[2].weight = 0.117;
    const FSecondaryMotionWeightedContactPoint contact = { 0, 3, prepared.Data(), 1 };

    gk::Array<gk::FVector3d> positions;
    if (!positions.AppendRange(initial, 4))
    {
        return Fail("could not allocate root-heavy weighted-contact positions");
    }
    const gk::FVector3d initialRootAnchor = EvaluateAnchorPoint(anchors[0], positions);
    gk::FVector3d initialCenter{};
    for (uint32_t index = 0; index < 3; ++index)
    {
        const gk::FVector3d anchorPosition = index == 2 ? anchors[index].constantPosition : EvaluateAnchorPoint(anchors[index], positions);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            initialCenter.value[axis] += anchorPosition.value[axis] * anchors[index].weight;
        }
    }
    if (fabs(anchors[0].weight + anchors[1].weight + anchors[2].weight - 1.0) > 1.0e-12 || Distance(initialCenter, { { 0.0, 0.0, 0.0 } }) >= body.radius)
    {
        return Fail("the root-heavy skin blend must start inside the independent body sphere with normalized weights");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 3, &contact, 1, positions, error))
    {
        return Fail(error.Empty() ? "the solver rejected a feasible root-heavy offset fixture" : error.CStr());
    }
    if (positions.Count() != 4u || Distance(positions.At(0), roots[0]) > 1.0e-12 || Distance(positions.At(2), roots[1]) > 1.0e-12)
    {
        return Fail("the root-heavy solve changed a fixed chain root");
    }
    for (uint32_t index = 0; index < 2; ++index)
    {
        const double length = Distance(positions.At(index * 2u), positions.At(index * 2u + 1u));
        const double dot = ((positions.At(index * 2u + 1u).value[0] - positions.At(index * 2u).value[0]) * references[index].direction.value[0] + (positions.At(index * 2u + 1u).value[1] - positions.At(index * 2u).value[1]) * references[index].direction.value[1] + (positions.At(index * 2u + 1u).value[2] - positions.At(index * 2u).value[2]) * references[index].direction.value[2]) / length;
        if (fabs(length - 1.0) > 0.0011 || dot < -0.001)
        {
            return Fail("the root-heavy solve changed a bone length or exceeded its direction cone");
        }
    }
    const gk::FVector3d finalRootAnchor = EvaluateAnchorPoint(anchors[0], positions);
    if (Distance(initialRootAnchor, finalRootAnchor) < 0.01)
    {
        return Fail("the dominant root anchor offset did not rotate with its child direction");
    }

    gk::Array<gk::FVector3d> floatPositions;
    for (uint32_t index = 0; index < positions.Count(); ++index)
    {
        const auto& position = positions.At(index);
        const gk::FVector3d stored = { { static_cast<float>(position.value[0]), static_cast<float>(position.value[1]), static_cast<float>(position.value[2]) } };
        if (!floatPositions.Append(stored))
        {
            return Fail("could not allocate float-rounded root-heavy positions");
        }
    }
    gk::FVector3d storedCenter{};
    for (uint32_t index = 0; index < 3; ++index)
    {
        const gk::FVector3d anchorPosition = index == 2 ? anchors[index].constantPosition : EvaluateAnchorPoint(anchors[index], floatPositions);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            storedCenter.value[axis] += anchorPosition.value[axis] * anchors[index].weight;
        }
    }
    if (Distance(storedCenter, { { 0.0, 0.0, 0.0 } }) < prepared.At(0).radius * 0.998)
    {
        return Fail("the float-rounded root-heavy skin blend remained inside the body sphere");
    }
    return true;
}

bool TestMultiSegmentWeightedOffsetsUseAncestorRotation()
{
    using namespace gk::model::animation;
    const FModelSecondaryMotionCollisionShape body = { { 0.65f, 1.1f, 0.05f }, { 0.65f, 1.1f, 0.05f }, 0.2f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&body, 1, prepared, error))
    {
        return Fail("could not prepare the two-segment weighted body sphere");
    }

    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 2;
    chain.cosineLimit = 0.0;
    chain.shapes = prepared.Data();
    chain.shapeCount = 1;
    const FSecondaryMotionReferenceSegment references[2] = { { { { 0.0, 1.0, 0.0 } }, 1.0 }, { { { 1.0, 0.0, 0.0 } }, 1.0 } };
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    anchors[0].chainIndex = 0;
    anchors[0].segmentIndex = 0;
    anchors[0].definition.direction = references[0].direction;
    anchors[0].definition.offset = { { 0.3, 1.0, 0.0 } };
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.5;
    anchors[1].chainIndex = 0;
    anchors[1].segmentIndex = 1;
    anchors[1].definition.direction = references[1].direction;
    anchors[1].definition.offset = { { 1.0, 0.2, 0.1 } };
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.5;
    const FSecondaryMotionWeightedContactPoint contact = { 0, 2, prepared.Data(), 1 };
    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d root = { { 0.0, 0.0, 0.0 } };
    const gk::FVector3d initial[3] = { root, { { 0.0, 1.0, 0.0 } }, { { 1.0, 1.0, 0.0 } } };
    if (!positions.AppendRange(initial, 3))
    {
        return Fail("could not allocate the two-segment weighted positions");
    }
    const gk::FVector3d initialFirstAnchor = { { 0.3, 1.0, 0.0 } };
    const gk::FVector3d initialSecondAnchor = { { 1.0, 1.2, 0.1 } };
    const gk::FVector3d initialBlend = WeightedPosition(initialFirstAnchor, initialSecondAnchor, 0.5, 0.5);
    const gk::FVector3d witness[3] = { root, { { 0.0, 0.0, 1.0 } }, { { 1.0, 0.0, 1.0 } } };
    gk::Array<gk::FVector3d> witnessPositions;
    if (!witnessPositions.AppendRange(witness, 3))
    {
        return Fail("could not allocate the independent two-segment witness");
    }
    const gk::FVector3d witnessBlend = WeightedPosition(EvaluateAnchorPoint(anchors[0], witnessPositions), EvaluateAnchorPoint(anchors[1], witnessPositions), 0.5, 0.5);
    const gk::FVector3d bodyCenter = { { body.start.x, body.start.y, body.start.z } };
    if (Distance(initialBlend, bodyCenter) >= body.radius || Distance(witnessBlend, bodyCenter) <= body.radius)
    {
        return Fail("the independent multi-segment witness does not separate the weighted point");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1, references, 2, anchors, 2, &contact, 1, positions, error))
    {
        return Fail(error.Empty() ? "the solver rejected a feasible multi-segment ancestor-offset fixture" : error.CStr());
    }
    if (Distance(positions.At(0), root) > 1.0e-12)
    {
        return Fail("the multi-segment solve moved its fixed root");
    }
    for (uint32_t segment = 0; segment < 2; ++segment)
    {
        const auto& parent = positions.At(segment);
        const auto& child = positions.At(segment + 1u);
        const double length = Distance(parent, child);
        const double directionDot = ((child.value[0] - parent.value[0]) * references[segment].direction.value[0] + (child.value[1] - parent.value[1]) * references[segment].direction.value[1] + (child.value[2] - parent.value[2]) * references[segment].direction.value[2]) / length;
        if (fabs(length - references[segment].length) > 0.0011 || directionDot < -0.001)
        {
            return Fail("the multi-segment solve changed a reference length or cone direction");
        }
    }
    const gk::FVector3d solvedBlend = WeightedPosition(EvaluateAnchorPoint(anchors[0], positions), EvaluateAnchorPoint(anchors[1], positions), 0.5, 0.5);
    if (Distance(solvedBlend, bodyCenter) < prepared.At(0).radius * 0.998)
    {
        return Fail("rotated ancestor offsets left the weighted point inside the body sphere");
    }
    return true;
}

double SegmentDistance(const gk::FVector3d& firstStart, const gk::FVector3d& firstEnd, const gk::FVector3d& secondStart, const gk::FVector3d& secondEnd)
{
    const double u[3] = { firstEnd.value[0] - firstStart.value[0], firstEnd.value[1] - firstStart.value[1], firstEnd.value[2] - firstStart.value[2] };
    const double v[3] = { secondEnd.value[0] - secondStart.value[0], secondEnd.value[1] - secondStart.value[1], secondEnd.value[2] - secondStart.value[2] };
    const double w[3] = { firstStart.value[0] - secondStart.value[0], firstStart.value[1] - secondStart.value[1], firstStart.value[2] - secondStart.value[2] };
    const double a = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
    const double b = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
    const double c = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    const double d = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
    const double e = v[0] * w[0] + v[1] * w[1] + v[2] * w[2];
    const double denominator = a * c - b * b;
    double firstAmount = denominator > 1.0e-20 ? (b * e - c * d) / denominator : 0.0;
    firstAmount = fmax(0.0, fmin(1.0, firstAmount));
    double secondAmount = c > 1.0e-20 ? (b * firstAmount + e) / c : 0.0;
    secondAmount = fmax(0.0, fmin(1.0, secondAmount));
    firstAmount = a > 1.0e-20 ? (b * secondAmount - d) / a : 0.0;
    firstAmount = fmax(0.0, fmin(1.0, firstAmount));
    const double delta[3] = { w[0] + u[0] * firstAmount - v[0] * secondAmount, w[1] + u[1] * firstAmount - v[1] * secondAmount, w[2] + u[2] * firstAmount - v[2] * secondAmount };
    return sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
}

bool TestWeightedSpheresCompeteWithChainBodyCapsules()
{
    using namespace gk::model::animation;
    const FModelSecondaryMotionCollisionShape bodyCapsules[2] = { { { -0.5f, -0.1f, 0.0f }, { -0.5f, 0.1f, 0.0f }, 0.18f }, { { 0.5f, -0.1f, 0.0f }, { 0.5f, 0.1f, 0.0f }, 0.18f } };
    const FModelSecondaryMotionCollisionShape weightedBody = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.25f };
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedChains[2];
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedWeightedBody;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&bodyCapsules[0], 1, preparedChains[0], error) || !PrepareSecondaryMotionCollisionShapes(&bodyCapsules[1], 1, preparedChains[1], error) || !PrepareSecondaryMotionCollisionShapes(&weightedBody, 1, preparedWeightedBody, error))
    {
        return Fail("could not prepare the competing chain capsules and weighted body sphere");
    }

    const gk::FVector3d roots[2] = { { { -1.0, 0.0, 0.0 } }, { { 1.0, 0.0, 0.0 } } };
    const gk::FVector3d initialChildren[2] = { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } } };
    const gk::FVector3d witnessChildren[2] = { { { -1.0, 1.0, 0.0 } }, { { 1.0, 1.0, 0.0 } } };
    const FSecondaryMotionReferenceSegment references[2] = { { { { 1.0, 0.0, 0.0 } }, 1.0 }, { { { -1.0, 0.0, 0.0 } }, 1.0 } };
    FSecondaryMotionContactChain chains[2]{};
    chains[0].firstPoint = 0;
    chains[0].firstSegment = 0;
    chains[0].segmentCount = 1;
    chains[0].cosineLimit = 0.0;
    chains[0].shapes = preparedChains[0].Data();
    chains[0].shapeCount = 1;
    chains[1].firstPoint = 2;
    chains[1].firstSegment = 1;
    chains[1].segmentCount = 1;
    chains[1].cosineLimit = 0.0;
    chains[1].shapes = preparedChains[1].Data();
    chains[1].shapeCount = 1;
    FSecondaryMotionWeightedContactAnchor anchors[2]{};
    anchors[0].chainIndex = 0;
    anchors[0].definition.direction = references[0].direction;
    anchors[0].definition.offset = references[0].direction;
    anchors[0].definition.cosineLimit = 0.0;
    anchors[0].weight = 0.5;
    anchors[1].chainIndex = 1;
    anchors[1].definition.direction = references[1].direction;
    anchors[1].definition.offset = references[1].direction;
    anchors[1].definition.cosineLimit = 0.0;
    anchors[1].weight = 0.5;
    const FSecondaryMotionWeightedContactPoint contact = { 0, 2, preparedWeightedBody.Data(), 1 };

    const gk::FVector3d initialWeighted = WeightedPosition(initialChildren[0], initialChildren[1], 0.5, 0.5);
    const gk::FVector3d witnessWeighted = WeightedPosition(witnessChildren[0], witnessChildren[1], 0.5, 0.5);
    const gk::FVector3d weightedCenter = { { weightedBody.start.x, weightedBody.start.y, weightedBody.start.z } };
    if (Distance(initialWeighted, weightedCenter) >= weightedBody.radius || Distance(witnessWeighted, weightedCenter) <= weightedBody.radius)
    {
        return Fail("the independent weighted-sphere input and clear witness are inconsistent");
    }
    for (uint32_t chain = 0; chain < 2; ++chain)
    {
        if (SegmentDistance(roots[chain], initialChildren[chain], gk::FVector3d{ { bodyCapsules[chain].start.x, bodyCapsules[chain].start.y, bodyCapsules[chain].start.z } }, gk::FVector3d{ { bodyCapsules[chain].end.x, bodyCapsules[chain].end.y, bodyCapsules[chain].end.z } }) >= bodyCapsules[chain].radius || SegmentDistance(roots[chain], witnessChildren[chain], gk::FVector3d{ { bodyCapsules[chain].start.x, bodyCapsules[chain].start.y, bodyCapsules[chain].start.z } }, gk::FVector3d{ { bodyCapsules[chain].end.x, bodyCapsules[chain].end.y, bodyCapsules[chain].end.z } }) <= bodyCapsules[chain].radius)
        {
            return Fail("the independent chain-capsule input and clear witness are inconsistent");
        }
    }

    gk::Array<gk::FVector3d> positions;
    const gk::FVector3d initial[4] = { roots[0], initialChildren[0], roots[1], initialChildren[1] };
    if (!positions.AppendRange(initial, 4))
    {
        return Fail("could not allocate the competing contact fixture positions");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 2, references, 2, anchors, 2, &contact, 1, positions, error, 32))
    {
        fprintf(stderr, "weighted capsule competition failed: %s\n", error.CStr());
        return Fail(error.Empty() ? "the solver rejected a known feasible capsule and weighted-sphere pose" : error.CStr());
    }

    if (positions.Count() != 4 || Distance(positions.At(0), roots[0]) > 1.0e-12 || Distance(positions.At(2), roots[1]) > 1.0e-12)
    {
        return Fail("the capsule competition solve moved a fixed chain root");
    }
    for (uint32_t chain = 0; chain < 2; ++chain)
    {
        const auto& child = positions.At(chain * 2u + 1u);
        const double length = Distance(roots[chain], child);
        const double dot = ((child.value[0] - roots[chain].value[0]) * references[chain].direction.value[0] + (child.value[1] - roots[chain].value[1]) * references[chain].direction.value[1] + (child.value[2] - roots[chain].value[2]) * references[chain].direction.value[2]) / length;
        const gk::FVector3d bodyStart = { { bodyCapsules[chain].start.x, bodyCapsules[chain].start.y, bodyCapsules[chain].start.z } };
        const gk::FVector3d bodyEnd = { { bodyCapsules[chain].end.x, bodyCapsules[chain].end.y, bodyCapsules[chain].end.z } };
        if (fabs(length - references[chain].length) > 0.0011 || dot < -0.001 || SegmentDistance(roots[chain], child, bodyStart, bodyEnd) < bodyCapsules[chain].radius * 0.997)
        {
            return Fail("the solve violated a chain length, cone, or independent capsule-clearance check");
        }
    }
    const gk::FVector3d solvedWeighted = WeightedPosition(EvaluateAnchorPoint(anchors[0], positions), EvaluateAnchorPoint(anchors[1], positions), 0.5, 0.5);
    if (Distance(solvedWeighted, weightedCenter) < weightedBody.radius * 0.997)
    {
        return Fail("the solve left the weighted offset-sphere contact inside the body sphere");
    }
    return true;
}

/**
 * 失敗姿勢からの継続と既知の成立姿勢の保存後制約を確認する。
 */
bool TestContactContinuationSeedAndStoredWitness()
{
    using namespace gk::model::animation;
    // 5鎖、12接触、共有形状12個の数値fixture。
    const FPreparedSecondaryMotionCollisionShape shapePool[12] = { { { { -0.058935802429914474, -0.03979945182800293, -0.12224793434143066 } }, { { 0.089211523532867432, -0.18739604949951172, -0.14142131805419922 } }, { { 0.14814732596278191, -0.14759659767150879, -0.019173383712768555 } }, 0.044100004477125046, 0.21000001065982127, 0.088088005781173706, 8.8088005781173704e-05, true, 0.087912006966769693, { -0.14702380821108818, -0.27548405528068542, -0.22950932383537292 }, { 0.17729952931404114, 0.048288553953170776, -0.034159928560256958 }, { -0.14702380821108818, -0.27548405528068554, -0.22950932383537337 }, { 0.17729952931404119, 0.048288553953170998, -0.034159928560256514 } }, { { { -0.12146702036261559, -0.011624932289123535, -3.337860107421875e-06 } }, { { 0.031974621117115021, -0.031529784202575684, 0.14198064804077148 } }, { { 0.15344164147973061, -0.019904851913452148, 0.14198398590087891 } }, 0.044099992721991624, 0.2099999826714079, 0.088088005781173706, 8.8088005781173704e-05, true, 0.087912006966769693, { -0.20955502614378929, -0.11961778998374939, -0.088091343641281128 }, { 0.12006262689828873, 0.076463073492050171, 0.23006865382194519 }, { -0.20955502614378929, -0.11961778998374961, -0.088091343641281572 }, { 0.12006262689828875, 0.076463073492050393, 0.23006865382194563 } }, { { { 0.10225260257720947, -0.23550212383270264, -0.15834736824035645 } }, { { -0.0025606304407119751, -0.47050881385803223, -0.29208612442016602 } }, { { -0.10481323301792145, -0.23500669002532959, -0.13373875617980957 } }, 0.084100013076852997, 0.29000002254629742, 0.078078001737594604, 7.8078001737594599e-05, true, 0.07792200160771609, { -0.08063863217830658, -0.54858681559562683, -0.37016412615776062 }, { 0.18033060431480408, -0.15742412209510803, -0.080269366502761841 }, { -0.08063863217830658, -0.54858681559562694, -0.37016412615776084 }, { 0.18033060431480413, -0.15742412209510781, -0.080269366502761397 } }, { { { 0.045806646347045898, -0.061910629272460938, 0.15833115577697754 } }, { { -0.059611175209283829, -0.32457154989242554, 0.095114946365356445 } }, { { -0.10541782155632973, -0.2626609206199646, -0.063216209411621094 } }, 0.084099965454983452, 0.28999994043962052, 0.078078001737594604, 7.8078001737594599e-05, true, 0.07792200160771609, { -0.13768917694687843, -0.40264955163002014, 0.017036944627761841 }, { 0.1238846480846405, 0.016167372465133667, 0.23640915751457214 }, { -0.13768917694687843, -0.40264955163002025, 0.017036944627761397 }, { 0.12388464808464053, 0.016167372465133889, 0.23640915751457259 } }, { { { -0.075419262051582336, -0.011369943618774414, -0.046967029571533203 } }, { { -0.075419262051582336, -0.011369943618774414, -0.046967029571533203 } }, { { 0, 0, 0 } }, 0, 0, 0.072000004351139069, 7.2000004351139066e-05, false, 0, { -0.14741926640272141, -0.083369947969913483, -0.11896703392267227 }, { -0.0034192577004432678, 0.060630060732364655, 0.025032974779605865 }, { -0.14741926640272141, -0.083369947969913705, -0.11896703392267272 }, { -0.0034192577004432539, 0.060630060732364877, 0.02503297477960631 } }, { { { -0.042343590408563614, 0.027901530265808105, -0.029801607131958008 } }, { { 0.041731193661689758, 0.079466819763183594, 0.018906116485595703 } }, { { 0.084074784070253372, 0.051565289497375488, 0.048707723617553711 } }, 0.012099990737411873, 0.10999995789731863, 0.082000002264976501, 8.2000002264976497e-05, false, 0, { -0.12434359267354012, -0.054098471999168396, -0.11180160939693451 }, { 0.12373119592666626, 0.1614668220281601, 0.1009061187505722 }, { -0.12434359267354012, -0.054098471999168618, -0.11180160939693495 }, { 0.12373119592666629, 0.16146682202816032, 0.10090611875057265 } }, { { { 0.048634946346282959, 0.083701014518737793, 0.022905826568603516 } }, { { 0.14324553310871124, 0.15953910350799561, 0.073870658874511719 } }, { { 0.094610586762428284, 0.075838088989257812, 0.050964832305908203 } }, 0.017299993001042901, 0.13152943777361364, 0.082000002264976501, 8.2000002264976497e-05, false, 0, { -0.033365055918693542, 0.0017010122537612915, -0.059094175696372986 }, { 0.22524553537368774, 0.24153910577297211, 0.15587066113948822 }, { -0.033365055918693549, 0.0017010122537610695, -0.05909417569637343 }, { 0.2252455353736878, 0.24153910577297233, 0.15587066113948866 } }, { { { 0.25894729793071747, 0.22000157833099365, 0.1338038444519043 } }, { { 0.25894729793071747, 0.22000157833099365, 0.1338038444519043 } }, { { 0, 0, 0 } }, 0, 0, 0.092000000178813934, 9.2000000178813941e-05, false, 0, { 0.16694729775190353, 0.12800157815217972, 0.041803844273090363 }, { 0.3509472981095314, 0.31200157850980759, 0.22580384463071823 }, { 0.16694729775190348, 0.1280015781521795, 0.041803844273089918 }, { 0.35094729810953146, 0.31200157850980781, 0.22580384463071868 } }, { { { -0.058935802429914474, -0.03979945182800293, -0.12224793434143066 } }, { { 0.089211523532867432, -0.18739604949951172, -0.14142131805419922 } }, { { 0.14814732596278191, -0.14759659767150879, -0.019173383712768555 } }, 0.044100004477125046, 0.21000001065982127, 0.052000008523464203, 5.2000008523464205e-05, false, 0, { -0.11093581095337868, -0.23939605802297592, -0.19342132657766342 }, { 0.14121153205633163, 0.012200556695461273, -0.070247925817966461 }, { -0.11093581095337868, -0.23939605802297614, -0.19342132657766387 }, { 0.14121153205633166, 0.012200556695461495, -0.070247925817966017 } }, { { { -0.12146702036261559, -0.011624932289123535, -3.337860107421875e-06 } }, { { 0.031974621117115021, -0.031529784202575684, 0.14198064804077148 } }, { { 0.15344164147973061, -0.019904851913452148, 0.14198398590087891 } }, 0.044099992721991624, 0.2099999826714079, 0.052000004798173904, 5.2000004798173906e-05, false, 0, { -0.17346702516078949, -0.083529789000749588, -0.052003342658281326 }, { 0.083974625915288925, 0.040375072509050369, 0.19398065283894539 }, { -0.17346702516078949, -0.08352978900074981, -0.05200334265828177 }, { 0.083974625915288953, 0.040375072509050591, 0.19398065283894583 } }, { { { 0.10225260257720947, -0.23550212383270264, -0.15834736824035645 } }, { { -0.0025606304407119751, -0.47050881385803223, -0.29208612442016602 } }, { { -0.10481323301792145, -0.23500669002532959, -0.13373875617980957 } }, 0.084100013076852997, 0.29000002254629742, 0.042000003159046173, 4.2000003159046176e-05, false, 0, { -0.044560633599758148, -0.5125088170170784, -0.33408612757921219 }, { 0.14425260573625565, -0.19350212067365646, -0.11634736508131027 }, { -0.044560633599758155, -0.51250881701707851, -0.33408612757921263 }, { 0.14425260573625567, -0.19350212067365624, -0.11634736508130983 } }, { { { 0.045806646347045898, -0.061910629272460938, 0.15833115577697754 } }, { { -0.059611175209283829, -0.32457154989242554, 0.095114946365356445 } }, { { -0.10541782155632973, -0.2626609206199646, -0.063216209411621094 } }, 0.084099965454983452, 0.28999994043962052, 0.041999999433755875, 4.1999999433755877e-05, false, 0, { -0.1016111746430397, -0.36657154932618141, 0.053114946931600571 }, { 0.087806645780801773, -0.019910629838705063, 0.20033115521073341 }, { -0.1016111746430397, -0.36657154932618152, 0.053114946931600127 }, { 0.087806645780801801, -0.019910629838704841, 0.20033115521073386 } } };
    const uint32_t chainShapeIndices[5][4] = { { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 } };
    const uint32_t contactShapeIndices[12][8] = { { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 }, { 4, 5, 6, 7, 8, 9, 10, 11 } };
    FPreparedSecondaryMotionCollisionShape chainShapes[5][4]{};
    FPreparedSecondaryMotionCollisionShape contactShapes[12][8]{};
    FSecondaryMotionContactChain chains[5]{};
    FSecondaryMotionReferenceSegment references[20]{};
    FSecondaryMotionWeightedContactAnchor anchors[36]{};
    FSecondaryMotionWeightedContactPoint contacts[12]{};
    const FSecondaryMotionReferenceSegment referenceValues[20] = { { { { -0.53563196141899161, -0.80301978658153628, -0.26126160120650743 } }, 0.093607418989181068 }, { { { -0.53379883546470419, -0.80467156208221191, -0.25992783697153105 } }, 0.062305980811149603 }, { { { -0.56608326686179256, -0.77403423403328875, -0.28358550654710951 } }, 0.036350602786894538 }, { { { -0.596320519020481, -0.7421079799940824, -0.30606794118993702 } }, 0.035000023641693676 }, { { { -0.42795011802524918, -0.80831787247913001, -0.40432773032898472 } }, 0.094989400989688832 }, { { { -0.48220229292313294, -0.80600075036701357, -0.34328375887519624 } }, 0.062327912616195684 }, { { { -0.52611164812818711, -0.77919466318905561, -0.34067904332602705 } }, 0.035988233652475103 }, { { { -0.56230539722724382, -0.74780451123336578, -0.35298874376973888 } }, 0.034998667043514842 }, { { { -0.24307089595825085, -0.80541430437351813, -0.54057778149733038 } }, 0.08955398120966121 }, { { { -0.45965617800479819, -0.76893842607364593, -0.44435334468124044 } }, 0.063121024784423413 }, { { { -0.53807787418080422, -0.72980583917904029, -0.42172934260879147 } }, 0.03681917203975664 }, { { { -0.57814207752759628, -0.70659298656151281, -0.40801726622063067 } }, 0.034998716911078698 }, { { { -0.61907782945840317, -0.7783220789006815, -0.10467751701675754 } }, 0.094989326503024302 }, { { { -0.5870025493813793, -0.78955348084105237, -0.17897851242967536 } }, 0.062327967453446126 }, { { { -0.60093569537027736, -0.76745123477425159, -0.22337164608185561 } }, 0.03598827054630533 }, { { { -0.62459768295158824, -0.73802787611222498, -0.2553283935107607 } }, 0.034998719258886708 }, { { { -0.66278428076413443, -0.73954261376279173, 0.11744666704878075 } }, 0.089554143011115944 }, { { { -0.66506357140394934, -0.73670156823286159, -0.12231616963699722 } }, 0.063120917454813755 }, { { { -0.6745521474755618, -0.7083885510696839, -0.20776202985507722 } }, 0.036819336602600554 }, { { { -0.67717102448418198, -0.69105111721279289, -0.25276027575153998 } }, 0.034998707501332826 } };
    memcpy(references, referenceValues, sizeof(references));
    chains[0].firstPoint = 0u;
    chains[0].firstSegment = 0u;
    chains[0].segmentCount = 4u;
    chains[0].cosineLimit = -1;
    chains[1].firstPoint = 5u;
    chains[1].firstSegment = 4u;
    chains[1].segmentCount = 4u;
    chains[1].cosineLimit = -1;
    chains[2].firstPoint = 10u;
    chains[2].firstSegment = 8u;
    chains[2].segmentCount = 4u;
    chains[2].cosineLimit = -1;
    chains[3].firstPoint = 15u;
    chains[3].firstSegment = 12u;
    chains[3].segmentCount = 4u;
    chains[3].cosineLimit = -1;
    chains[4].firstPoint = 20u;
    chains[4].firstSegment = 16u;
    chains[4].segmentCount = 4u;
    chains[4].cosineLimit = -1;
    const FSecondaryMotionWeightedContactAnchor anchorValues[36] = { { 4294967295u, 0u, { { { 0, 0, 0 } }, { { 0, 0, 0 } }, -1 }, { { -0.054815612328871902, -0.043680262740985798, -0.0039209836432791967 } }, 0.11424469202756882 }, { 0u, 0u, { { { -0.53563196141899161, -0.80301978658153628, -0.26126160120650743 } }, { { -0.054815574863049721, -0.043680356628363504, -0.003921095539722419 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.63846737146377563 }, { 3u, 0u, { { { -0.61907782945840317, -0.7783220789006815, -0.10467751701675754 } }, { { -0.030197186351345495, -0.053328265628751925, -0.040343404977630559 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.24728794395923615 }, { 4294967295u, 0u, { { { 0, 0, 0 } }, { { 0, 0, 0 } }, -1 }, { { -0.031815249581468641, -0.047113360701116491, -0.040517036066151757 } }, 0.11531767249107361 }, { 0u, 0u, { { { -0.53563196141899161, -0.80301978658153628, -0.26126160120650743 } }, { { -0.031815132667988022, -0.047113346869729497, -0.040517110036948689 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.65450781583786011 }, { 1u, 0u, { { { -0.42795011802524918, -0.80831787247913001, -0.40432773032898472 } }, { { -0.054997266148410495, -0.049259376545192135, -0.0019981123579017046 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.23017454147338867 }, { 4294967295u, 0u, { { { 0, 0, 0 } }, { { 0, 0, 0 } }, -1 }, { { -0.079680422574987322, -0.034879303539993023, 0.024898262123703052 } }, 0.12004899233579636 }, { 0u, 0u, { { { -0.53563196141899161, -0.80301978658153628, -0.26126160120650743 } }, { { -0.079680462974046184, -0.034879495275914107, 0.024898116941344695 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.16638565063476562 }, { 3u, 0u, { { { -0.61907782945840317, -0.7783220789006815, -0.10467751701675754 } }, { { -0.055062062693960467, -0.04452732627210787, -0.011524234351058482 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.71356534957885742 }, { 4294967295u, 0u, { { { 0, 0, 0 } }, { { 0, 0, 0 } }, -1 }, { { -0.068120437179099602, -0.039372374442460112, 0.011767740807173688 } }, 0.12185923010110855 }, { 0u, 0u, { { { -0.53563196141899161, -0.80301978658153628, -0.26126160120650743 } }, { { -0.068120441500200357, -0.039372519722231547, 0.011767610447014798 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.40765026211738586 }, { 3u, 0u, { { { -0.61907782945840317, -0.7783220789006815, -0.10467751701675754 } }, { { -0.043502045502039899, -0.049020380143116937, -0.024654724380016757 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.47049048542976379 }, { 4294967295u, 0u, { { { 0, 0, 0 } }, { { 0, 0, 0 } }, -1 }, { { -0.099824595166974894, -0.021408695469567585, 0.04518679263702241 } }, 0.14583724737167358 }, { 3u, 0u, { { { -0.61907782945840317, -0.7783220789006815, -0.10467751701675754 } }, { { -0.075206291111899526, -0.031056793917578651, 0.0087642826661428197 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.55512070655822754 }, { 4u, 0u, { { { -0.66278428076413443, -0.73954261376279173, 0.11744666704878075 } }, { { -0.037508182177684764, -0.055865650753258389, -0.014063757641284421 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.29904201626777649 }, { 4294967295u, 0u, { { { 0, 0, 0 } }, { { 0, 0, 0 } }, -1 }, { { -0.10856847649529011, -0.011497679723561749, 0.052325217069979946 } }, 0.16803304851055145 }, { 3u, 0u, { { { -0.61907782945840317, -0.7783220789006815, -0.10467751701675754 } }, { { -0.083950202207329738, -0.021145851665399285, 0.015902725942809282 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.2517702579498291 }, { 4u, 0u, { { { -0.66278428076413443, -0.73954261376279173, 0.11744666704878075 } }, { { -0.046252100002652619, -0.045954672353368226, -0.0069254146604108158 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.58019667863845825 }, { 0u, 0u, { { { -0.53563196141899161, -0.80301978658153628, -0.26126160120650743 } }, { { -0.073756131012882836, -0.085787581585705264, -0.0066863208270389052 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.15207086503505707 }, { 3u, 0u, { { { -0.61907782945840317, -0.7783220789006815, -0.10467751701675754 } }, { { -0.049137504311375202, -0.095435066600259466, -0.043108647317340154 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.071231454610824585 }, { 0u, 1u, { { { -0.53379883546470419, -0.80467156208221191, -0.25992783697153105 } }, { { -0.023617039081536711, -0.010619062220975348, 0.017769678783506694 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.50396150350570679 }, { 3u, 1u, { { { -0.5870025493813793, -0.78955348084105237, -0.17897851242967536 } }, { { 0.0096682880072296307, -0.021502841653282645, -0.033165209635354714 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.27273622155189514 }, { 3u, 0u, { { { -0.61907782945840317, -0.7783220789006815, -0.10467751701675754 } }, { { -0.073616741571609412, -0.053204602399703149, 0.030879750823555263 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.46209827065467834 }, { 4u, 0u, { { { -0.66278428076413443, -0.73954261376279173, 0.11744666704878075 } }, { { -0.0359184768081309, -0.078013605209653364, 0.0080517017763363091 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.43255537748336792 }, { 3u, 1u, { { { -0.5870025493813793, -0.78955348084105237, -0.17897851242967536 } }, { { -0.014811172358262237, 0.020727504101448802, 0.040822964871954567 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.056557107716798782 }, { 4u, 1u, { { { -0.66506357140394934, -0.73670156823286159, -0.12231616963699722 } }, { { 0.023436484009859902, -0.011784614985261573, -0.0024659980294473444 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.048789218068122864 }, { 3u, 0u, { { { -0.61907782945840317, -0.7783220789006815, -0.10467751701675754 } }, { { -0.052260430221094945, -0.066602983766466561, 0.013341796838466702 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.90802276134490967 }, { 3u, 1u, { { { -0.5870025493813793, -0.78955348084105237, -0.17897851242967536 } }, { { 0.0065451378031560203, 0.0073291378994757572, 0.02328500947673709 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.091977238655090332 }, { 0u, 1u, { { { -0.53379883546470419, -0.80467156208221191, -0.25992783697153105 } }, { { -0.054588771694827662, -0.03926024630870989, 0.022898875227321196 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.12626338005065918 }, { 3u, 1u, { { { -0.5870025493813793, -0.78955348084105237, -0.17897851242967536 } }, { { -0.021303018247785244, -0.050143389889506285, -0.028035966089553332 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.1900000274181366 }, { 0u, 2u, { { { -0.56608326686179256, -0.77403423403328875, -0.28358550654710951 } }, { { -0.021329937557773793, 0.01087564591685844, 0.039093850411164921 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.27283546328544617 }, { 3u, 2u, { { { -0.60093569537027736, -0.76745123477425159, -0.22337164608185561 } }, { { 0.01528379945357064, -0.00093206782674942756, -0.016880586603073289 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.41090115904808044 }, { 1u, 2u, { { { -0.52611164812818711, -0.77919466318905561, -0.34067904332602705 } }, { { 0.003172450390947934, -0.0038775375112097277, -0.053134804997151869 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.44372114539146423 }, { 2u, 2u, { { { -0.53807787418080422, -0.72980583917904029, -0.42172934260879147 } }, { { -0.023491316168530951, -0.028065937455183709, 0.010364026911536595 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.55627882480621338 }, { 1u, 2u, { { { -0.52611164812818711, -0.77919466318905561, -0.34067904332602705 } }, { { -0.01247698244109283, -0.024530490537468617, -0.066674085101375874 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.44915756583213806 }, { 2u, 2u, { { { -0.53807787418080422, -0.72980583917904029, -0.42172934260879147 } }, { { -0.039140808801486515, -0.048718913406902487, -0.0031752475021739057 } }, -1 }, { { -0.092192307114601135, -1.2451411485671997, -2.3451688289642334 } }, 0.55084246397018433 } };
    memcpy(anchors, anchorValues, sizeof(anchors));
    contacts[0].firstAnchor = 0u;
    contacts[0].anchorCount = 3u;
    contacts[1].firstAnchor = 3u;
    contacts[1].anchorCount = 3u;
    contacts[2].firstAnchor = 6u;
    contacts[2].anchorCount = 3u;
    contacts[3].firstAnchor = 9u;
    contacts[3].anchorCount = 3u;
    contacts[4].firstAnchor = 12u;
    contacts[4].anchorCount = 3u;
    contacts[5].firstAnchor = 15u;
    contacts[5].anchorCount = 3u;
    contacts[6].firstAnchor = 18u;
    contacts[6].anchorCount = 4u;
    contacts[7].firstAnchor = 22u;
    contacts[7].anchorCount = 4u;
    contacts[8].firstAnchor = 26u;
    contacts[8].anchorCount = 2u;
    contacts[9].firstAnchor = 28u;
    contacts[9].anchorCount = 4u;
    contacts[10].firstAnchor = 32u;
    contacts[10].anchorCount = 2u;
    contacts[11].firstAnchor = 34u;
    contacts[11].anchorCount = 2u;
    // 各鎖が使う共有形状の数。
    const uint32_t chainShapeCounts[5] = { 4u, 4u, 4u, 4u, 4u };
    for (uint32_t chainIndex = 0; chainIndex < 5u; ++chainIndex)
    {
        for (uint32_t shapeIndex = 0; shapeIndex < chainShapeCounts[chainIndex]; ++shapeIndex)
        {
            chainShapes[chainIndex][shapeIndex] = shapePool[chainShapeIndices[chainIndex][shapeIndex]];
        }
        chains[chainIndex].shapes = chainShapes[chainIndex];
        chains[chainIndex].shapeCount = chainShapeCounts[chainIndex];
        chains[chainIndex].attachments = nullptr;
        chains[chainIndex].attachmentCount = 0u;
    }
    // 各接触点が使う共有形状の数。
    const uint32_t contactShapeCounts[12] = { 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u };
    for (uint32_t contactIndex = 0; contactIndex < 12u; ++contactIndex)
    {
        for (uint32_t shapeIndex = 0; shapeIndex < contactShapeCounts[contactIndex]; ++shapeIndex)
        {
            contactShapes[contactIndex][shapeIndex] = shapePool[contactShapeIndices[contactIndex][shapeIndex]];
        }
        contacts[contactIndex].shapes = contactShapes[contactIndex];
        contacts[contactIndex].shapeCount = contactShapeCounts[contactIndex];
    }
    const gk::FVector3d initialValues[25] = { { { 0, 0, 0 } }, { { 0.092855334281921387, -0.011804461479187012, 0.00094342231750488281 } }, { { 0.047716766595840454, -0.052825808525085449, -0.011776208877563477 } }, { { 0.016782172024250031, -0.071910858154296875, -0.012203216552734375 } }, { { -0.010081790387630463, -0.094063162803649902, -0.015753030776977539 } }, { { 0.023182578384876251, 0.0021464824676513672, -0.038518667221069336 } }, { { 0.065807029604911804, -0.081225991249084473, -0.022544622421264648 } }, { { 0.048098713159561157, -0.13826525211334229, -0.040369033813476562 } }, { { 0.042967408895492554, -0.10707509517669678, -0.057573556900024414 } }, { { 0.016237467527389526, -0.1273047924041748, -0.067632436752319336 } }, { { 0.029923111200332642, 0.019980907440185547, -0.085361480712890625 } }, { { 0.045677855610847473, -0.010259747505187988, -0.16816973686218262 } }, { { 0.023016512393951416, -0.055069565773010254, -0.20641636848449707 } }, { { -0.013389952480792999, -0.059808492660522461, -0.20362997055053711 } }, { { -0.03546612337231636, -0.085623860359191895, -0.21206331253051758 } }, { { -0.024617880582809448, 0.009648442268371582, 0.036422491073608398 } }, { { -0.0087004154920578003, 0.055033445358276367, 0.11833596229553223 } }, { { -0.055378645658493042, 0.045388102531433105, 0.15849661827087402 } }, { { -0.020289771258831024, 0.039296507835388184, 0.16367530822753906 } }, { { -0.054402723908424377, 0.046824932098388672, 0.16580605506896973 } }, { { -0.062316130846738815, 0.034457206726074219, 0.059250354766845703 } }, { { -0.13109687715768814, -9.8824501037597656e-05, 0.10502195358276367 } }, { { -0.16302195936441422, -0.052219748497009277, 0.089259624481201172 } }, { { -0.18952152132987976, -0.077675938606262207, 0.086931943893432617 } }, { { -0.21556733548641205, -0.10038328170776367, 0.081372976303100586 } } };
    gk::Array<gk::FVector3d> positions;
    if (!positions.Reserve(25u))
        return Fail("solver初期位置を確保できませんでした");
    for (uint32_t index = 0; index < 25u; ++index)
        if (!positions.Append(initialValues[index]))
            return Fail("solver初期位置を追加できませんでした");
    const uint32_t pointCount = 25u;
    gk::FVector3d inputPositions[25]{};
    memcpy(inputPositions, initialValues, sizeof(inputPositions));
    gk::String error;
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 5u, references, 20u, anchors, 36u, contacts, 12u, positions, error, 32u))
    {
        return Fail(error.Empty() ? "継続fixtureがsolverに拒否されました" : error.CStr());
    }
    if (!HasCapturedFixtureConstraints(chains, 5u, references, anchors, contacts, 12u, inputPositions, positions, error))
    {
        return Fail("solver結果がfloat保存後の制約を満たしません");
    }
    // 独立に得た成立姿勢を保持する。
    const gk::FVector3d witnessValues[25] = { { { 0, 0, 0 } }, { { 0.093243184655260325, 0.0046502029984096804, 0.0068141776638639406 } }, { { 0.094737806294210219, -0.054695449535401952, -0.012103932081345636 } }, { { 0.059004769603370005, -0.059858438086508148, -0.0078778273686346267 } }, { { 0.031710014200137721, -0.080998952801192292, -0.013629055063228535 } }, { { 0.023182578384876251, 0.0021464824676513672, -0.038518667221069336 } }, { { 0.077928567566015822, -0.07307304586370833, -0.019338325030008097 } }, { { 0.07354930464961712, -0.13424407982615882, -0.03046016621597758 } }, { { 0.07643206617118567, -0.10449855454484691, -0.050511260280235781 } }, { { 0.050072094145460794, -0.12571466516630414, -0.041570610431152843 } }, { { 0.029923111200332642, 0.019980907440185547, -0.085361480712890625 } }, { { 0.053948309927836363, -0.0085359684573240724, -0.16678318031226258 } }, { { 0.039252790388437236, -0.053408152514846874, -0.20867342468026218 } }, { { 0.002640922718200156, -0.049617547667578199, -0.20774936284136336 } }, { { -0.019016761982130231, -0.07458780130631637, -0.21925333224587717 } }, { { -0.024617880582809448, 0.009648442268371582, 0.036422491073608398 } }, { { -0.00015130342649116724, 0.061022715926552173, 0.11248195963503393 } }, { { -0.056830863445237703, 0.063139267777315045, 0.13832230040720983 } }, { { -0.021940741859556592, 0.055460593284298643, 0.14266660595364167 } }, { { -0.054355166208590489, 0.068628827233312428, 0.14356830354550354 } }, { { -0.062316130846738815, 0.034457206726074219, 0.059250354766845703 } }, { { -0.13490700890415441, 0.0065596167709254249, 0.10366018957855383 } }, { { -0.16622166254857795, -0.04580270703073186, 0.087479035925071003 } }, { { -0.19015871211372504, -0.073136670072209142, 0.081517855637708081 } }, { { -0.21620452138272031, -0.095844008912642886, 0.075958889090524728 } } };
    gk::Array<gk::FVector3d> witnessPositions;
    if (!witnessPositions.Reserve(pointCount))
        return Fail("成立姿勢の位置配列を確保できませんでした");
    for (uint32_t index = 0; index < pointCount; ++index)
        if (!witnessPositions.Append(witnessValues[index]))
            return Fail("成立姿勢の位置を追加できませんでした");
    gk::FVector3d witnessInput[25]{};
    memcpy(witnessInput, witnessValues, sizeof(witnessInput));
    if (!HasCapturedFixtureConstraints(chains, 5u, references, anchors, contacts, 12u, witnessInput, witnessPositions, error))
    {
        return Fail("独立に得た成立姿勢がfloat保存後の制約を満たしません");
    }
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 5u, references, 20u, anchors, 36u, contacts, 12u, witnessPositions, error, 32u))
    {
        return Fail(error.Empty() ? "solverが既知の成立姿勢を拒否しました" : error.CStr());
    }
    if (memcmp(witnessInput, witnessPositions.Data(), sizeof(witnessInput)) != 0 || !HasCapturedFixtureConstraints(chains, 5u, references, anchors, contacts, 12u, witnessInput, witnessPositions, error))
    {
        return Fail("既知の成立姿勢が変化するかfloat保存後の制約を満たしません");
    }
    return true;
}

/**
 * 複数鎖の複雑な接触姿勢と成立witnessの保存後制約を確認する。
 */
bool TestCurrentInputContactContinuationAndStoredWitness()
{
    using namespace gk::model::animation;
    // 344個の形状参照を共有shape pool 20個へまとめる。
    const FPreparedSecondaryMotionCollisionShape shapePool[20] = { { { { 0.0167730450630188, 1.2337712049484253, 2.2982017993927 } }, { { 0.0167730450630188, 1.2337712049484253, 2.2982017993927 } }, { { 0.0, 0.0, 0.0 } }, 0.0, 0.0, 0.07307300716638565, 7.307300716638565e-05, true, 0.0729270063713193, { -0.05629996210336685, 1.1606981977820396, 2.2251287922263145 }, { 0.08984605222940445, 1.306844212114811, 2.371274806559086 }, { -0.05629996210336686, 1.1606981977820394, 2.225128792226314 }, { 0.08984605222940446, 1.3068442121148112, 2.3712748065590863 } }, { { { 0.04984871670603752, 1.2730426788330078, 2.3153672218322754 } }, { { 0.1339235007762909, 1.3246079683303833, 2.364074945449829 } }, { { 0.08407478407025337, 0.05156528949737549, 0.04870772361755371 } }, 0.012099990737411873, 0.10999995789731863, 0.08308300375938416, 8.308300375938415e-05, true, 0.08291700428724288, { -0.033234287053346634, 1.1899596750736237, 2.2322842180728912 }, { 0.21700650453567505, 1.4076909720897675, 2.4471579492092133 }, { -0.03323428705334664, 1.1899596750736234, 2.232284218072891 }, { 0.21700650453567508, 1.4076909720897677, 2.4471579492092137 } }, { { { 0.1408272534608841, 1.3288421630859375, 2.368074655532837 } }, { { 0.23543784022331238, 1.4046802520751953, 2.419039487838745 } }, { { 0.09461058676242828, 0.07583808898925781, 0.0509648323059082 } }, 0.0172999930010429, 0.13152943777361364, 0.08308300375938416, 8.308300375938415e-05, true, 0.08291700428724288, { 0.05774424970149994, 1.2457591593265533, 2.2849916517734528 }, { 0.31852084398269653, 1.4877632558345795, 2.5021224915981293 }, { 0.05774424970149993, 1.2457591593265531, 2.2849916517734523 }, { 0.3185208439826966, 1.4877632558345797, 2.5021224915981297 } }, { { { 0.3511396050453186, 1.4651427268981934, 2.4789726734161377 } }, { { 0.3511396050453186, 1.4651427268981934, 2.4789726734161377 } }, { { 0.0, 0.0, 0.0 } }, 0.0, 0.0, 0.09309300035238266, 9.309300035238266e-05, true, 0.09290700220316649, { 0.25804660469293594, 1.3720497265458107, 2.385879673063755 }, { 0.44423260539770126, 1.558235727250576, 2.5720656737685204 }, { 0.2580466046929359, 1.3720497265458105, 2.3858796730637546 }, { 0.4442326053977013, 1.5582357272505762, 2.572065673768521 } }, { { { 0.03325650468468666, 1.2053416967391968, 2.2229208946228027 } }, { { 0.18140383064746857, 1.057745099067688, 2.203747510910034 } }, { { 0.1481473259627819, -0.1475965976715088, -0.019173383712768555 } }, 0.044100004477125046, 0.21000001065982127, 0.05305301025509834, 5.305301025509835e-05, true, 0.0529470105394721, { -0.019796505570411682, 1.0046920888125896, 2.150694500654936 }, { 0.2344568409025669, 1.2583947069942951, 2.275973904877901 }, { -0.019796505570411686, 1.0046920888125894, 2.1506945006549354 }, { 0.23445684090256694, 1.2583947069942953, 2.2759739048779015 } }, { { { -0.02927471324801445, 1.2335162162780762, 2.345165491104126 } }, { { 0.12416692823171616, 1.213611364364624, 2.487149477005005 } }, { { 0.1534416414797306, -0.01990485191345215, 0.1419839859008789 } }, 0.044099992721991624, 0.2099999826714079, 0.053053006529808044, 5.305300652980805e-05, true, 0.052947006817907094, { -0.0823277197778225, 1.160558357834816, 2.292112484574318 }, { 0.1772199347615242, 1.2865692228078842, 2.540202483534813 }, { -0.08232771977782251, 1.1605583578348158, 2.2921124845743175 }, { 0.17721993476152423, 1.2865692228078844, 2.5402024835348134 } }, { { { 0.1944449096918106, 1.009639024734497, 2.186821460723877 } }, { { 0.08963167667388916, 0.7746323347091675, 2.0530827045440674 } }, { { -0.10481323301792145, -0.2350066900253296, -0.13373875617980957 } }, 0.084100013076853, 0.2900000225462974, 0.04304300621151924, 4.304300621151924e-05, true, 0.04295700518041849, { 0.04658867046236992, 0.7315893284976482, 2.010039698332548 }, { 0.23748791590332985, 1.0526820309460163, 2.229864466935396 }, { 0.04658867046236991, 0.7315893284976481, 2.0100396983325477 }, { 0.23748791590332988, 1.0526820309460165, 2.2298644669353966 } }, { { { 0.13799895346164703, 1.1832305192947388, 2.503499984741211 } }, { { 0.032581131905317307, 0.9205695986747742, 2.44028377532959 } }, { { -0.10541782155632973, -0.2626609206199646, -0.0632162094116211 } }, 0.08409996545498345, 0.2899999404396205, 0.04304300248622894, 4.304300248622894e-05, true, 0.042957001458853485, { -0.010461870580911636, 0.8775265961885452, 2.397240772843361 }, { 0.18104195594787598, 1.2262735217809677, 2.54654298722744 }, { -0.010461870580911638, 0.8775265961885451, 2.3972407728433605 }, { 0.181041955947876, 1.226273521780968, 2.5465429872274403 } }, { { { 0.03325650468468666, 1.2053416967391968, 2.2229208946228027 } }, { { 0.18140383064746857, 1.057745099067688, 2.203747510910034 } }, { { 0.1481473259627819, -0.1475965976715088, -0.019173383712768555 } }, 0.044100004477125046, 0.21000001065982127, 0.0880880057811737, 8.80880057811737e-05, true, 0.08791200696676969, { -0.054831501096487045, 0.9696570932865143, 2.1156595051288605 }, { 0.2694918364286423, 1.2934297025203705, 2.3110089004039764 }, { -0.05483150109648705, 0.9696570932865142, 2.11565950512886 }, { 0.26949183642864233, 1.2934297025203707, 2.311008900403977 } }, { { { -0.02927471324801445, 1.2335162162780762, 2.345165491104126 } }, { { 0.12416692823171616, 1.213611364364624, 2.487149477005005 } }, { { 0.1534416414797306, -0.01990485191345215, 0.1419839859008789 } }, 0.044099992721991624, 0.2099999826714079, 0.0880880057811737, 8.80880057811737e-05, true, 0.08791200696676969, { -0.11736271902918816, 1.1255233585834503, 2.2570774853229523 }, { 0.21225493401288986, 1.3216042220592499, 2.5752374827861786 }, { -0.11736271902918817, 1.12552335858345, 2.257077485322952 }, { 0.2122549340128899, 1.32160422205925, 2.575237482786179 } }, { { { 0.1944449096918106, 1.009639024734497, 2.186821460723877 } }, { { 0.08963167667388916, 0.7746323347091675, 2.0530827045440674 } }, { { -0.10481323301792145, -0.2350066900253296, -0.13373875617980957 } }, 0.084100013076853, 0.2900000225462974, 0.0780780017375946, 7.80780017375946e-05, true, 0.07792200160771609, { 0.011553674936294556, 0.6965543329715729, 1.9750047028064728 }, { 0.2725229114294052, 1.0877170264720917, 2.2648994624614716 }, { 0.011553674936294554, 0.6965543329715728, 1.9750047028064726 }, { 0.27252291142940527, 1.087717026472092, 2.264899462461472 } }, { { { 0.13799895346164703, 1.1832305192947388, 2.503499984741211 } }, { { 0.032581131905317307, 0.9205695986747742, 2.44028377532959 } }, { { -0.10541782155632973, -0.2626609206199646, -0.0632162094116211 } }, 0.08409996545498345, 0.2899999404396205, 0.0780780017375946, 7.80780017375946e-05, true, 0.07792200160771609, { -0.0454968698322773, 0.8424915969371796, 2.3622057735919952 }, { 0.21607695519924164, 1.2613085210323334, 2.5815779864788055 }, { -0.045496869832277305, 0.8424915969371795, 2.362205773591995 }, { 0.21607695519924167, 1.2613085210323336, 2.581577986478806 } }, { { { 0.0167730450630188, 1.2337712049484253, 2.2982017993927 } }, { { 0.0167730450630188, 1.2337712049484253, 2.2982017993927 } }, { { 0.0, 0.0, 0.0 } }, 0.0, 0.0, 0.07200000435113907, 7.200000435113907e-05, false, 0.0, { -0.05522695928812027, 1.1617712005972862, 2.226201795041561 }, { 0.08877304941415787, 1.3057712092995644, 2.3702018037438393 }, { -0.05522695928812028, 1.161771200597286, 2.2262017950415607 }, { 0.08877304941415788, 1.3057712092995646, 2.3702018037438397 } }, { { { 0.04984871670603752, 1.2730426788330078, 2.3153672218322754 } }, { { 0.1339235007762909, 1.3246079683303833, 2.364074945449829 } }, { { 0.08407478407025337, 0.05156528949737549, 0.04870772361755371 } }, 0.012099990737411873, 0.10999995789731863, 0.0820000022649765, 8.20000022649765e-05, false, 0.0, { -0.03215128555893898, 1.1910426765680313, 2.233367219567299 }, { 0.2159235030412674, 1.4066079705953598, 2.4460749477148056 }, { -0.03215128555893899, 1.191042676568031, 2.2333672195672984 }, { 0.21592350304126742, 1.40660797059536, 2.446074947714806 } }, { { { 0.1408272534608841, 1.3288421630859375, 2.368074655532837 } }, { { 0.23543784022331238, 1.4046802520751953, 2.419039487838745 } }, { { 0.09461058676242828, 0.07583808898925781, 0.0509648323059082 } }, 0.0172999930010429, 0.13152943777361364, 0.0820000022649765, 8.20000022649765e-05, false, 0.0, { 0.05882725119590759, 1.246842160820961, 2.2860746532678604 }, { 0.3174378424882889, 1.4866802543401718, 2.5010394901037216 }, { 0.058827251195907586, 1.2468421608209608, 2.28607465326786 }, { 0.31743784248828893, 1.486680254340172, 2.501039490103722 } }, { { { 0.3511396050453186, 1.4651427268981934, 2.4789726734161377 } }, { { 0.3511396050453186, 1.4651427268981934, 2.4789726734161377 } }, { { 0.0, 0.0, 0.0 } }, 0.0, 0.0, 0.09200000017881393, 9.200000017881394e-05, false, 0.0, { 0.25913960486650467, 1.3731427267193794, 2.3869726732373238 }, { 0.44313960522413254, 1.5571427270770073, 2.5709726735949516 }, { 0.2591396048665046, 1.3731427267193792, 2.3869726732373233 }, { 0.4431396052241326, 1.5571427270770075, 2.570972673594952 } }, { { { 0.03325650468468666, 1.2053416967391968, 2.2229208946228027 } }, { { 0.18140383064746857, 1.057745099067688, 2.203747510910034 } }, { { 0.1481473259627819, -0.1475965976715088, -0.019173383712768555 } }, 0.044100004477125046, 0.21000001065982127, 0.0520000085234642, 5.2000008523464205e-05, false, 0.0, { -0.018743503838777542, 1.0057450905442238, 2.15174750238657 }, { 0.23340383917093277, 1.257341705262661, 2.274920903146267 }, { -0.018743503838777546, 1.0057450905442236, 2.1517475023865695 }, { 0.2334038391709328, 1.2573417052626612, 2.2749209031462674 } }, { { { -0.02927471324801445, 1.2335162162780762, 2.345165491104126 } }, { { 0.12416692823171616, 1.213611364364624, 2.487149477005005 } }, { { 0.1534416414797306, -0.01990485191345215, 0.1419839859008789 } }, 0.044099992721991624, 0.2099999826714079, 0.052000004798173904, 5.2000004798173906e-05, false, 0.0, { -0.08127471804618835, 1.1616113595664501, 2.293165486305952 }, { 0.17616693302989006, 1.28551622107625, 2.539149481803179 }, { -0.08127471804618837, 1.16161135956645, 2.2931654863059516 }, { 0.1761669330298901, 1.2855162210762503, 2.5391494818031792 } }, { { { 0.1944449096918106, 1.009639024734497, 2.186821460723877 } }, { { 0.08963167667388916, 0.7746323347091675, 2.0530827045440674 } }, { { -0.10481323301792145, -0.2350066900253296, -0.13373875617980957 } }, 0.084100013076853, 0.2900000225462974, 0.04200000315904617, 4.2000003159046176e-05, false, 0.0, { 0.04763167351484299, 0.7326323315501213, 2.011082701385021 }, { 0.23644491285085678, 1.0516390278935432, 2.228821463882923 }, { 0.04763167351484298, 0.7326323315501212, 2.0110827013850208 }, { 0.2364449128508568, 1.0516390278935435, 2.2288214638829236 } }, { { { 0.13799895346164703, 1.1832305192947388, 2.503499984741211 } }, { { 0.032581131905317307, 0.9205695986747742, 2.44028377532959 } }, { { -0.10541782155632973, -0.2626609206199646, -0.0632162094116211 } }, 0.08409996545498345, 0.2899999404396205, 0.041999999433755875, 4.199999943375588e-05, false, 0.0, { -0.009418867528438568, 0.8785695992410183, 2.398283775895834 }, { 0.1799989528954029, 1.2252305187284946, 2.545499984174967 }, { -0.00941886752843857, 0.8785695992410182, 2.3982837758958335 }, { 0.17999895289540294, 1.2252305187284949, 2.5454999841749673 } } };
    const uint32_t chainShapeIndices[13][8] = { { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 8, 9, 10, 11 }, { 8, 9, 10, 11 }, { 8, 9, 10, 11 }, { 8, 9, 10, 11 }, { 8, 9, 10, 11 }, { 8, 9, 10, 11 }, { 8, 9, 10, 11 }, { 8, 9, 10, 11 }, { 8, 9, 10, 11 }, { 8, 9, 10, 11 } };
    const uint32_t contactShapeIndices[35][8] = { { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 12, 13, 14, 15, 16, 17, 18, 19 } };
    FPreparedSecondaryMotionCollisionShape chainShapes[13][8]{};
    FPreparedSecondaryMotionCollisionShape contactShapes[35][8]{};
    FSecondaryMotionContactChain chains[13]{};
    FSecondaryMotionReferenceSegment references[76]{};
    FSecondaryMotionWeightedContactAnchor anchors[115]{};
    FSecondaryMotionWeightedContactPoint contacts[35]{};
    const FSecondaryMotionReferenceSegment referenceValues[76] = { { { { -0.6182049940428369, -0.11474353610387553, -0.7775966218180701 } }, 0.03892623334220055 }, { { { -0.7764310752178185, -0.35758724671830217, -0.5189278817143345 } }, 0.03953382386812201 }, { { { -0.7697697697912198, -0.47597215541403265, -0.42532929453082874 } }, 0.04516468064246801 }, { { { -0.7394772337230531, -0.3285694298891489, -0.58755046638363 } }, 0.0468282852957754 }, { { { -0.6232827554645334, -0.2563563998110328, -0.7387827847320652 } }, 0.048208921155931735 }, { { { -0.47017092006539435, -0.23450322330088075, -0.8508510705090274 } }, 0.052248027159562206 }, { { { -0.21132624807343772, -0.2608754949747397, -0.9419581694517483 } }, 0.05048066867008536 }, { { { -0.5809242664728976, -0.4830780986279596, -0.6551049894855949 } }, 0.05200731778725302 }, { { { -0.9322971172010207, -0.24673261717637102, -0.26447136117161074 } }, 0.046208630958941496 }, { { { -0.9103914939847362, 0.2312379487151534, 0.3430981474071932 } }, 0.03458512606831986 }, { { { -0.38306709333915956, 0.4957223401717005, 0.7794350283733599 } }, 0.03499982839661698 }, { { { -0.9629443654608072, -0.2690313455330156, 0.018981152440734346 } }, 0.03892593871434906 }, { { { -0.8567457448948812, -0.3935293768218348, -0.3333487035863996 } }, 0.039533937939624085 }, { { { -0.7697705503320571, -0.47597262541101965, -0.42532735592814 } }, 0.045164886498898604 }, { { { -0.8835589550719678, -0.39305025161583473, -0.2546273210338229 } }, 0.04682839975543501 }, { { { -0.9196576274991556, -0.3889969713699263, -0.053955578466444554 } }, 0.048209041065433535 }, { { { -0.8955607620802847, -0.424882628406473, 0.13208207107925918 } }, 0.05224801303996507 }, { { { -0.7766538918707783, -0.5138830174836314, 0.3643253718640673 } }, 0.05048062032906507 }, { { { -0.8000874463214155, -0.5811630543578239, -0.14869291337639784 } }, 0.05200723045612651 }, { { { -0.8002927594335932, -0.1876555077849968, -0.5694882874968721 } }, 0.04620852233049727 }, { { { -0.4211485308736579, 0.4501946945605539, -0.7873745309142842 } }, 0.03458513551057147 }, { { { 0.21705084234309072, 0.7643010492499211, -0.6072337589047765 } }, 0.03499988046877396 }, { { { -0.8438164240929792, -0.08736859915339339, -0.5294719731136966 } }, 0.04477958570559427 }, { { { -0.8415156956915485, -0.13410761870192836, -0.5233225396549123 } }, 0.06278799257583384 }, { { { -0.8415775327393031, -0.13320327052867187, -0.5234540525288658 } }, 0.0574454745488252 }, { { { -0.8372463736815393, -0.18138447657343235, -0.5158664375740041 } }, 0.061022766357345874 }, { { { -0.8401566041358519, -0.1517059037537036, -0.5206939593400194 } }, 0.05205479279904896 }, { { { -0.8392699356769718, -0.16162119040869632, -0.5191382916715424 } }, 0.057377438069815455 }, { { { -0.8398937837266635, -0.15473823008152113, -0.5202254436381851 } }, 0.05681573254682364 }, { { { -0.8405952532440129, -0.14638788526826058, -0.5215076291583092 } }, 0.0567382200032503 }, { { { -0.8423282351794408, 0.014899410840153601, -0.5387588994866861 } }, 0.06531167178641242 }, { { { -0.7893140359119396, 0.30334679925237934, -0.533820261975641 } }, 0.056318780175715205 }, { { { -0.6343714260685736, 0.6165206507779383, -0.46634234308292427 } }, 0.06099444020693375 }, { { { -0.37488156530162764, 0.8678670836584232, -0.3259916212101546 } }, 0.06502997316525562 }, { { { -0.06221619441280651, 0.9883882297184805, -0.13862846932270456 } }, 0.050476400132878504 }, { { { -0.08074228998652067, 0.9853617953248287, -0.15014264857782014 } }, 0.03499994140075289 }, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, 0.09360741898918107 }, { { { -0.5337988354647042, -0.8046715620822119, -0.25992783697153105 } }, 0.0623059808111496 }, { { { -0.5660832668617926, -0.7740342340332887, -0.2835855065471095 } }, 0.03635060278689454 }, { { { -0.596320519020481, -0.7421079799940824, -0.306067941189937 } }, 0.035000023641693676 }, { { { -0.4279501180252492, -0.80831787247913, -0.4043277303289847 } }, 0.09498940098968883 }, { { { -0.48220229292313294, -0.8060007503670136, -0.34328375887519624 } }, 0.062327912616195684 }, { { { -0.5261116481281871, -0.7791946631890556, -0.34067904332602705 } }, 0.0359882336524751 }, { { { -0.5623053972272438, -0.7478045112333658, -0.3529887437697389 } }, 0.03499866704351484 }, { { { -0.24307089595825085, -0.8054143043735181, -0.5405777814973304 } }, 0.08955398120966121 }, { { { -0.4596561780047982, -0.7689384260736459, -0.44435334468124044 } }, 0.06312102478442341 }, { { { -0.5380778741808042, -0.7298058391790403, -0.42172934260879147 } }, 0.03681917203975664 }, { { { -0.5781420775275963, -0.7065929865615128, -0.40801726622063067 } }, 0.0349987169110787 }, { { { -0.2573436667635529, -0.5728112831047888, -0.7782425528872955 } }, 0.08537718518162234 }, { { { -0.49311571073569427, -0.680457890123909, -0.5420460825370378 } }, 0.06317467238010961 }, { { { -0.550934726375684, -0.6706182071121853, -0.4967314642369542 } }, 0.03530070571252955 }, { { { -0.581563005433046, -0.685140432575723, -0.4385966921462518 } }, 0.034998763264170646 }, { { { -0.5387117960710657, -0.23828158773222144, -0.8080912607631012 } }, 0.08291013879345466 }, { { { -0.5941056134182747, -0.5951925734390202, -0.541095481988098 } }, 0.06507502409658854 }, { { { -0.5594656968048568, -0.6346200685624709, -0.5331561710009748 } }, 0.04102728646502332 }, { { { -0.5600727191571867, -0.6612455499318113, -0.4990720107872725 } }, 0.03499900127555725 }, { { { -0.8431203216698211, -0.1060907758340961, -0.5271554519022799 } }, 0.08283574850490648 }, { { { -0.7422985330296479, -0.5210493424155498, -0.4213080471939217 } }, 0.06543398268314206 }, { { { -0.7473746444311653, -0.5101638153996237, -0.42563367150436765 } }, 0.04224145003632175 }, { { { -0.7301553469965508, -0.5457838820512597, -0.41108773193249526 } }, 0.03500003355484167 }, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, 0.0949893265030243 }, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, 0.062327967453446126 }, { { { -0.6009356953702774, -0.7674512347742516, -0.2233716460818556 } }, 0.03598827054630533 }, { { { -0.6245976829515882, -0.738027876112225, -0.2553283935107607 } }, 0.03499871925888671 }, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, 0.08955414301111594 }, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, 0.06312091745481375 }, { { { -0.6745521474755618, -0.7083885510696839, -0.20776202985507722 } }, 0.036819336602600554 }, { { { -0.677171024484182, -0.6910511172127929, -0.25276027575154 } }, 0.034998707501332826 }, { { { -0.8618780924733325, -0.4779344858073627, 0.1695428588604707 } }, 0.08537727445604651 }, { { { -0.7592910875352086, -0.6386843794823684, -0.12473695440735336 } }, 0.06317462947452161 }, { { { -0.7422191858010115, -0.6405980783547296, -0.19683694327313428 } }, 0.03530064506079086 }, { { { -0.7042252091436586, -0.6658895313385769, -0.2462884220994983 } }, 0.034998743486835834 }, { { { -0.978385058946948, -0.16927834632880856, -0.1187750726944036 } }, 0.08290999759308028 }, { { { -0.79374347902065, -0.5638612613474069, -0.2281047291572585 } }, 0.06507511172479755 }, { { { -0.775400460924714, -0.6007330355987252, -0.19461229441656994 } }, 0.041027211664446665 }, { { { -0.747370662690892, -0.631851745029791, -0.2054275172701664 } }, 0.03499896589769507 } };
    memcpy(references, referenceValues, sizeof(references));
    chains[0].firstPoint = 0u;
    chains[0].firstSegment = 0u;
    chains[0].segmentCount = 11u;
    chains[0].cosineLimit = 0.7071067811865476;
    chains[1].firstPoint = 12u;
    chains[1].firstSegment = 11u;
    chains[1].segmentCount = 11u;
    chains[1].cosineLimit = 0.7071067811865476;
    chains[2].firstPoint = 24u;
    chains[2].firstSegment = 22u;
    chains[2].segmentCount = 14u;
    chains[2].cosineLimit = 0.7071067811865476;
    chains[3].firstPoint = 39u;
    chains[3].firstSegment = 36u;
    chains[3].segmentCount = 4u;
    chains[3].cosineLimit = -1.0;
    chains[4].firstPoint = 44u;
    chains[4].firstSegment = 40u;
    chains[4].segmentCount = 4u;
    chains[4].cosineLimit = -1.0;
    chains[5].firstPoint = 49u;
    chains[5].firstSegment = 44u;
    chains[5].segmentCount = 4u;
    chains[5].cosineLimit = -1.0;
    chains[6].firstPoint = 54u;
    chains[6].firstSegment = 48u;
    chains[6].segmentCount = 4u;
    chains[6].cosineLimit = -1.0;
    chains[7].firstPoint = 59u;
    chains[7].firstSegment = 52u;
    chains[7].segmentCount = 4u;
    chains[7].cosineLimit = -1.0;
    chains[8].firstPoint = 64u;
    chains[8].firstSegment = 56u;
    chains[8].segmentCount = 4u;
    chains[8].cosineLimit = -1.0;
    chains[9].firstPoint = 69u;
    chains[9].firstSegment = 60u;
    chains[9].segmentCount = 4u;
    chains[9].cosineLimit = -1.0;
    chains[10].firstPoint = 74u;
    chains[10].firstSegment = 64u;
    chains[10].segmentCount = 4u;
    chains[10].cosineLimit = -1.0;
    chains[11].firstPoint = 79u;
    chains[11].firstSegment = 68u;
    chains[11].segmentCount = 4u;
    chains[11].cosineLimit = -1.0;
    chains[12].firstPoint = 84u;
    chains[12].firstSegment = 72u;
    chains[12].segmentCount = 4u;
    chains[12].cosineLimit = -1.0;
    const FSecondaryMotionWeightedContactAnchor anchorValues[115] = { { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.07530446663796778, 1.1994237466503888, 2.2716678951128264 } }, 0.11187124997377396 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.016887649964374196, -0.045717297439717086, -0.07350097412717838 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.22415263950824738 }, { 4u, 0u, { { { -0.4279501180252492, -0.80831787247913, -0.4043277303289847 } }, { { -0.0400697954476362, -0.047863267877836804, -0.03498195787004832 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.6639761328697205 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.037376694785729234, 1.201460885826214, 2.341247845320954 } }, 0.11424469202756882 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.05481557486304972, -0.043680356628363504, -0.003921095539722419 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.6384673714637756 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.030197186351345495, -0.053328265628751925, -0.04034340497763056 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.24728794395923615 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.060377057533132494, 1.1980277878660832, 2.3046517928980816 } }, 0.11531767249107361 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.03181513266798802, -0.0471133468697295, -0.04051711003694869 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.6545078158378601 }, { 4u, 0u, { { { -0.4279501180252492, -0.80831787247913, -0.4043277303289847 } }, { { -0.054997266148410495, -0.049259376545192135, -0.0019981123579017046 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.23017454147338867 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.06849827417019433, 1.1983889793317735, 2.286926158400672 } }, 0.11863411962985992 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.02369387532494105, -0.04675210593879717, -0.058242727037788766 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.4448210895061493 }, { 4u, 0u, { { { -0.4279501180252492, -0.80831787247913, -0.4043277303289847 } }, { { -0.046876017302492884, -0.04889810182346861, -0.019723719850870847 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.436544805765152 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.048836004653129386, 1.1998701070111264, 2.322500580830988 } }, 0.11954724043607712 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.04335622391921329, -0.04527107955045721, -0.022668340799377726 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.877383828163147 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.01873781857078194, -0.05491905794300256, -0.05909059412881887 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.003068920923396945 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.012511884539613813, 1.2102618450272067, 2.3700670910879365 } }, 0.12004899233579636 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.07968046297404618, -0.03487949527591411, 0.024898116941344695 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.16638565063476562 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.05506206269396047, -0.04452732627210787, -0.011524234351058482 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.7135653495788574 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.024071869935501533, 1.2057687741247396, 2.356936569771407 } }, 0.12185923010110855 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.06812044150020036, -0.03937251972223155, 0.011767610447014798 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.40765026211738586 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.0435020455020399, -0.04902038014311694, -0.024654724380016757 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.4704904854297638 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.0016240996531609042, 1.2162763810782657, 2.381042732818782 } }, 0.1257588416337967 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.06594987307908029, -0.038512820402187176, -0.0005486075462090567 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.8008853197097778 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.02825177015607308, -0.06332169187307568, -0.023376543271976538 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.07335585355758667 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.0814091094878599, 1.2024558845494342, 2.2566276723391323 } }, 0.12991662323474884 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.010782984510659288, -0.04268512195674096, -0.08854117773329118 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.017555322498083115 }, { 4u, 0u, { { { -0.4279501180252492, -0.80831787247913, -0.4043277303289847 } }, { { -0.03396511863299079, -0.04483108018817206, -0.050022154970839755 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.8525280356407166 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.08716272592163557, 1.2072592003610954, 2.243186273388805 } }, 0.14057600498199463 }, { 4u, 0u, { { { -0.4279501180252492, -0.80831787247913, -0.4043277303289847 } }, { { -0.028211477566129295, -0.04002774602268333, -0.06346353383190914 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.6181451082229614 }, { 5u, 0u, { { { -0.24307089595825085, -0.8054143043735181, -0.5405777814973304 } }, { { -0.034952059195916374, -0.05786210000909438, -0.016620721330057264 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.24127890169620514 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { -0.007632288052373759, 1.2237324530976321, 2.390355621601256 } }, 0.14583724737167358 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.07520629111189953, -0.03105679391757865, 0.00876428266614282 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5551207065582275 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.037508182177684764, -0.05586565075325839, -0.014063757641284421 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.2990420162677765 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { -0.016376169380688976, 1.233643468843638, 2.3974940460342133 } }, 0.16803304851055145 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.08395020220732974, -0.021145851665399285, 0.01590272594280928 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.2517702579498291 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.04625210000265262, -0.045954672353368226, -0.006925414660410816 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5801966786384583 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.09162123889298363, 1.2146075330240247, 2.231319345231688 } }, 0.1708361804485321 }, { 4u, 0u, { { { -0.4279501180252492, -0.80831787247913, -0.4043277303289847 } }, { { -0.023752939019606623, -0.03267943144493318, -0.07533044063455802 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.32385843992233276 }, { 5u, 0u, { { { -0.24307089595825085, -0.8054143043735181, -0.5405777814973304 } }, { { -0.030493604817904654, -0.050513742702144246, -0.028487592334200965 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5053053498268127 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { -0.06538818480734443, 1.3100232476578997, 2.376252985350876 } }, 0.1846226453781128 }, { 11u, 0u, { { { -0.8618780924733325, -0.4779344858073627, 0.1695428588604707 } }, { { -0.06311680343800813, -0.006865652640062336, -0.018925730617045722 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.3480122685432434 }, { 12u, 0u, { { { -0.978385058946948, -0.16927834632880856, -0.1187750726944036 } }, { { -0.055033949733403864, -0.040212795097874565, 0.02735225214129075 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.4673651158809662 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.03450300410579277, 1.3098742218963122, 2.185946552402583 } }, 0.3781258165836334 }, { 6u, 0u, { { { -0.2573436667635529, -0.5728112831047888, -0.7782425528872955 } }, { { -0.06896717435561461, 0.009581309261083859, -0.043450343824935955 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.04108308255672455 }, { 7u, 0u, { { { -0.5387117960710657, -0.23828158773222144, -0.8080912607631012 } }, { { -0.02686513272056179, -0.02910496082783509, -0.050507347755778975 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5807911157608032 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { -0.07493805601673098, 1.3327526159839165, 2.3334849089968386 } }, 0.422169029712677 }, { 8u, 0u, { { { -0.8431203216698211, -0.1060907758340961, -0.5271554519022799 } }, { { -0.08647040813820699, -0.022785092440165328, 0.05081528095268173 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.04076797142624855 }, { 12u, 0u, { { { -0.978385058946948, -0.16927834632880856, -0.1187750726944036 } }, { { -0.0645836626457672, -0.017483369653876535, -0.015415796194341302 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5370630025863647 }, { 4294967295u, 0u, { { { 0.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, -1.0 }, { { 0.017494743233111065, 1.319019204435866, 2.1902598351991256 } }, 0.43547436594963074 }, { 7u, 0u, { { { -0.5387117960710657, -0.23828158773222144, -0.8080912607631012 } }, { { -0.04387339265280832, -0.019959992420511163, -0.0461940742359126 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.508491575717926 }, { 8u, 0u, { { { -0.8431203216698211, -0.1060907758340961, -0.5271554519022799 } }, { { 0.0059627708729846005, -0.036518170658561944, -0.0924095862220054 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.05603408440947533 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.0897787282975655, -0.08059990007800136, 0.012206950468806932 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.06936387717723846 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.06516009985786836, -0.09024733565016596, -0.02421540670122785 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.1465279906988144 }, { 3u, 1u, { { { -0.5337988354647042, -0.8046715620822119, -0.25992783697153105 } }, { { -0.039639639737175575, -0.0054313867323005915, 0.03666294858530457 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.24775011837482452 }, { 9u, 1u, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, { { -0.006354308872577319, -0.01631508005484958, -0.014272006958632705 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5363579988479614 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.03627751090816317, -0.08948684605795626, -0.07210379820700163 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.07431954890489578 }, { 4u, 0u, { { { -0.4279501180252492, -0.80831787247913, -0.4043277303289847 } }, { { -0.05945953222153624, -0.09163239272189026, -0.03358461514608582 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.1360366940498352 }, { 3u, 1u, { { { -0.5337988354647042, -0.8046715620822119, -0.25992783697153105 } }, { { 0.013861580382928396, -0.014318327789438756, -0.04764779890603116 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.28471675515174866 }, { 4u, 1u, { { { -0.48220229292313294, -0.8060007503670136, -0.34328375887519624 } }, { { -0.01880874636900509, -0.014850654278889402, 0.004822243475988183 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5049269795417786 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.07375613101288284, -0.08578758158570526, -0.006686320827038905 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.15207086503505707 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.0491375043113752, -0.09543506660025947, -0.043108647317340154 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.07123145461082458 }, { 3u, 1u, { { { -0.5337988354647042, -0.8046715620822119, -0.25992783697153105 } }, { { -0.02361703908153671, -0.010619062220975348, 0.017769678783506694 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5039615035057068 }, { 9u, 1u, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, { { 0.00966828800722963, -0.021502841653282645, -0.033165209635354714 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.27273622155189514 }, { 3u, 0u, { { { -0.5356319614189916, -0.8030197865815363, -0.26126160120650743 } }, { { -0.04605752177516146, -0.08992175430663646, -0.05075746444647722 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.15543392300605774 }, { 4u, 0u, { { { -0.4279501180252492, -0.80831787247913, -0.4043277303289847 } }, { { -0.06923953974068475, -0.09206735580255193, -0.012238301302972293 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.06871901452541351 }, { 3u, 1u, { { { -0.5337988354647042, -0.8046715620822119, -0.25992783697153105 } }, { { 0.004081566528278305, -0.014753240114045219, -0.02630146666677103 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.515632688999176 }, { 4u, 1u, { { { -0.48220229292313294, -0.8060007503670136, -0.34328375887519624 } }, { { -0.028588725525710457, -0.015285651195757144, 0.026168547281732193 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.2602143883705139 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.07996803425916765, -0.04173402659781833, 0.03896133656627132 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.36110374331474304 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.042269769560252225, -0.06654300201230534, 0.01613318847958474 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.6388962864875793 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.07782719416974371, -0.06840198751424653, 0.021345734454908505 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.36466050148010254 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.04012895019309139, -0.09321100630118112, -0.0014822491953122713 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.19020220637321472 }, { 9u, 1u, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, { { -0.019021544481330754, 0.005530216200650431, 0.031288983212531415 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.2815113663673401 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { 0.019226089609827246, -0.026981919591539283, -0.011999864792519258 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.16362592577934265 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.07361674157160941, -0.05320460239970315, 0.030879750823555263 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.46209827065467834 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.0359184768081309, -0.07801360520965336, 0.008051701776336309 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.4325553774833679 }, { 9u, 1u, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, { { -0.014811172358262237, 0.0207275041014488, 0.04082296487195457 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.05655710771679878 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { 0.0234364840098599, -0.011784614985261573, -0.0024659980294473444 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.048789218068122864 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.06837889112791129, -0.06328158485644986, 0.02112289176288068 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5691195726394653 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.030680632655343817, -0.08809060175218592, -0.001705061894874918 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.19753561913967133 }, { 9u, 1u, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, { { -0.00957328851821393, 0.01065056392657393, 0.03106612196745631 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.16539952158927917 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { 0.028674343476457413, -0.02186158080859288, -0.0122226738715115 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.06794526427984238 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.06443042122092424, -0.0735069701428468, 0.01275425205787778 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.5784373879432678 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.026732171155638376, -0.09831600394314388, -0.010073617293105352 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.05317092686891556 }, { 9u, 1u, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, { { -0.005624785682202038, 0.00042522569542849387, 0.022697494609327608 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.32433265447616577 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { 0.03262282946640359, -0.03208694627226491, -0.020591153093411405 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.04405904561281204 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.06432845155868444, -0.07244199357409153, 0.0106730715455991 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.6040772199630737 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.02663021787493742, -0.09725102062175417, -0.01215479208685534 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.04213980957865715 }, { 9u, 1u, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, { { -0.005522814239793672, 0.0014901925004189923, 0.02061632611791312 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.3193944990634918 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { 0.03272478199778647, -0.031021965263059836, -0.022672309778110897 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.034388430416584015 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.05765646732475016, -0.056301939487577446, 0.02385092315918691 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.8034232258796692 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.01995818863577729, -0.08111096529725065, 0.0010229873251050015 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.1965768039226532 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.052260430221094945, -0.06660298376646656, 0.013341796838466702 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.9080227613449097 }, { 9u, 1u, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, { { 0.00654513780315602, 0.007329137899475757, 0.02328500947673709 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.09197723865509033 }, { 9u, 0u, { { { -0.6190778294584032, -0.7783220789006815, -0.10467751701675754 } }, { { -0.05217278542789516, -0.06557018445768903, 0.011553835533274048 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.9179160594940186 }, { 9u, 1u, { { { -0.5870025493813793, -0.7895534808410524, -0.17897851242967536 } }, { { 0.006632781508702612, 0.00836192237512906, 0.021497058215106418 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.08208394050598145 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.10844170947954816, -0.030980318185701745, 0.007001138911920772 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.11318590492010117 }, { 11u, 0u, { { { -0.8618780924733325, -0.4779344858073627, 0.1695428588604707 } }, { { -0.07629415515447538, -0.06827101286368409, 0.016241971136077615 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.2965259850025177 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { -0.049086633474517194, 0.035248739494966874, -0.0035165766064839676 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.15368661284446716 }, { 11u, 1u, { { { -0.7592910875352086, -0.6386843794823684, -0.12473695440735336 } }, { { -0.002709390765534725, -0.027466228992124267, 0.0017669790750786951 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.4366014897823334 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.10576485042782174, -0.019804142489471155, 0.01704365795883442 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.13403292000293732 }, { 11u, 0u, { { { -0.8618780924733325, -0.4779344858073627, 0.1695428588604707 } }, { { -0.07361726526884324, -0.05709486247749884, 0.026284483556876126 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.4630168080329895 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { -0.04640983068556048, 0.04642484143919606, 0.006525853952395362 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.08580219745635986 }, { 11u, 1u, { { { -0.7592910875352086, -0.6386843794823684, -0.12473695440735336 } }, { { -3.253377319234474e-05, -0.016290168099183183, 0.01180940511338309 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.3171480894088745 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.1064222636400202, -0.020306024779554732, 0.01956027377955133 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.13747024536132812 }, { 11u, 0u, { { { -0.8618780924733325, -0.4779344858073627, 0.1695428588604707 } }, { { -0.07427467079734404, -0.05759675585031632, 0.028801106203832583 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.4424585700035095 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { -0.04706723793839605, 0.045922962571327414, 0.009042448246573034 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.09389618039131165 }, { 11u, 1u, { { { -0.7592910875352086, -0.6386843794823684, -0.12473695440735336 } }, { { -0.0006899299013859994, -0.01679206374225163, 0.014326011036145517 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.3261749744415283 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.0972737319706218, -0.049048968417169984, 0.009346462540637384 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.21979103982448578 }, { 11u, 0u, { { { -0.8618780924733325, -0.4779344858073627, 0.1695428588604707 } }, { { -0.06512622910853483, -0.0863396571774997, 0.01858738322335496 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.16502097249031067 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { -0.0379186270423262, 0.017180132815532455, -0.0011712677987025522 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.34009236097335815 }, { 11u, 1u, { { { -0.7592910875352086, -0.6386843794823684, -0.12473695440735336 } }, { { 0.008458512002756269, -0.04553482393113564, 0.004112446886270174 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.2750956118106842 }, { 10u, 0u, { { { -0.6627842807641344, -0.7395426137627917, 0.11744666704878075 } }, { { -0.09738139475190813, -0.03858220182102823, 0.02513274280040223 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.26291796565055847 }, { 11u, 0u, { { { -0.8618780924733325, -0.4779344858073627, 0.1695428588604707 } }, { { -0.06523384350258274, -0.07587293716167476, 0.0343736681784948 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.26291796565055847 }, { 10u, 1u, { { { -0.6650635714039493, -0.7367015682328616, -0.12231616963699722 } }, { { -0.038026332960670414, 0.027646846881770826, 0.014614872832270105 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.23708203434944153 }, { 11u, 1u, { { { -0.7592910875352086, -0.6386843794823684, -0.12473695440735336 } }, { { 0.008350899730981451, -0.03506819853210236, 0.019898606199385382 } }, -1.0 }, { { 0.0, 0.0, 0.0 } }, 0.23708203434944153 } };
    memcpy(anchors, anchorValues, sizeof(anchors));
    contacts[0].firstAnchor = 0u;
    contacts[0].anchorCount = 3u;
    contacts[1].firstAnchor = 3u;
    contacts[1].anchorCount = 3u;
    contacts[2].firstAnchor = 6u;
    contacts[2].anchorCount = 3u;
    contacts[3].firstAnchor = 9u;
    contacts[3].anchorCount = 3u;
    contacts[4].firstAnchor = 12u;
    contacts[4].anchorCount = 3u;
    contacts[5].firstAnchor = 15u;
    contacts[5].anchorCount = 3u;
    contacts[6].firstAnchor = 18u;
    contacts[6].anchorCount = 3u;
    contacts[7].firstAnchor = 21u;
    contacts[7].anchorCount = 3u;
    contacts[8].firstAnchor = 24u;
    contacts[8].anchorCount = 3u;
    contacts[9].firstAnchor = 27u;
    contacts[9].anchorCount = 3u;
    contacts[10].firstAnchor = 30u;
    contacts[10].anchorCount = 3u;
    contacts[11].firstAnchor = 33u;
    contacts[11].anchorCount = 3u;
    contacts[12].firstAnchor = 36u;
    contacts[12].anchorCount = 3u;
    contacts[13].firstAnchor = 39u;
    contacts[13].anchorCount = 3u;
    contacts[14].firstAnchor = 42u;
    contacts[14].anchorCount = 3u;
    contacts[15].firstAnchor = 45u;
    contacts[15].anchorCount = 3u;
    contacts[16].firstAnchor = 48u;
    contacts[16].anchorCount = 3u;
    contacts[17].firstAnchor = 51u;
    contacts[17].anchorCount = 4u;
    contacts[18].firstAnchor = 55u;
    contacts[18].anchorCount = 4u;
    contacts[19].firstAnchor = 59u;
    contacts[19].anchorCount = 4u;
    contacts[20].firstAnchor = 63u;
    contacts[20].anchorCount = 4u;
    contacts[21].firstAnchor = 67u;
    contacts[21].anchorCount = 2u;
    contacts[22].firstAnchor = 69u;
    contacts[22].anchorCount = 4u;
    contacts[23].firstAnchor = 73u;
    contacts[23].anchorCount = 4u;
    contacts[24].firstAnchor = 77u;
    contacts[24].anchorCount = 4u;
    contacts[25].firstAnchor = 81u;
    contacts[25].anchorCount = 4u;
    contacts[26].firstAnchor = 85u;
    contacts[26].anchorCount = 4u;
    contacts[27].firstAnchor = 89u;
    contacts[27].anchorCount = 2u;
    contacts[28].firstAnchor = 91u;
    contacts[28].anchorCount = 2u;
    contacts[29].firstAnchor = 93u;
    contacts[29].anchorCount = 2u;
    contacts[30].firstAnchor = 95u;
    contacts[30].anchorCount = 4u;
    contacts[31].firstAnchor = 99u;
    contacts[31].anchorCount = 4u;
    contacts[32].firstAnchor = 103u;
    contacts[32].anchorCount = 4u;
    contacts[33].firstAnchor = 107u;
    contacts[33].anchorCount = 4u;
    contacts[34].firstAnchor = 111u;
    contacts[34].anchorCount = 4u;
    // 各鎖が使う共有形状の数。
    const uint32_t chainShapeCounts[13] = { 8u, 8u, 8u, 4u, 4u, 4u, 4u, 4u, 4u, 4u, 4u, 4u, 4u };
    for (uint32_t chainIndex = 0; chainIndex < 13u; ++chainIndex)
    {
        for (uint32_t shapeIndex = 0; shapeIndex < chainShapeCounts[chainIndex]; ++shapeIndex)
        {
            chainShapes[chainIndex][shapeIndex] = shapePool[chainShapeIndices[chainIndex][shapeIndex]];
        }
        chains[chainIndex].shapes = chainShapes[chainIndex];
        chains[chainIndex].shapeCount = chainShapeCounts[chainIndex];
        chains[chainIndex].attachments = nullptr;
        chains[chainIndex].attachmentCount = 0u;
    }
    // 各接触点が使う共有形状の数。
    const uint32_t contactShapeCounts[35] = { 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u };
    for (uint32_t contactIndex = 0; contactIndex < 35u; ++contactIndex)
    {
        for (uint32_t shapeIndex = 0; shapeIndex < contactShapeCounts[contactIndex]; ++shapeIndex)
        {
            contactShapes[contactIndex][shapeIndex] = shapePool[contactShapeIndices[contactIndex][shapeIndex]];
        }
        contacts[contactIndex].shapes = contactShapes[contactIndex];
        contacts[contactIndex].shapeCount = contactShapeCounts[contactIndex];
    }
    const gk::FVector3d inputValues[89] = { { { 0.3778441548347473, 1.530260443687439, 2.3947529792785645 } }, { { 0.3654409348964691, 1.5110902786254883, 2.3632266521453857 } }, { { 0.3300803303718567, 1.5071117877960205, 2.346000909805298 } }, { { 0.2910069227218628, 1.4922990798950195, 2.3288631439208984 } }, { { 0.2536635994911194, 1.4904593229293823, 2.3006677627563477 } }, { { 0.2207116186618805, 1.4884387254714966, 2.2655367851257324 } }, { { 0.1902434229850769, 1.4858922958374023, 2.2231686115264893 } }, { { 0.17639116942882538, 1.4796336889266968, 2.1750309467315674 } }, { { 0.1356498897075653, 1.463518738746643, 2.1470091342926025 } }, { { 0.09094483405351639, 1.4647878408432007, 2.1353867053985596 } }, { { 0.06712200492620468, 1.4782824516296387, 2.1565170288085938 } }, { { 0.05721881240606308, 1.4951159954071045, 2.185560941696167 } }, { { 0.301247239112854, 1.4959800243377686, 2.5717427730560303 } }, { { 0.27370554208755493, 1.4701159000396729, 2.5811092853546143 } }, { { 0.2394227236509323, 1.4617211818695068, 2.563300848007202 } }, { { 0.19998914003372192, 1.448317050933838, 2.5458312034606934 } }, { { 0.15452826023101807, 1.4411088228225708, 2.5372142791748047 } }, { { 0.10738113522529602, 1.4311699867248535, 2.5387892723083496 } }, { { 0.05784760043025017, 1.4178297519683838, 2.5487051010131836 } }, { { 0.01896037347614765, 1.3948551416397095, 2.57124924659729 } }, { { -0.02753390371799469, 1.3775392770767212, 2.555654525756836 } }, { { -0.06498365104198456, 1.3825572729110718, 2.5290544033050537 } }, { { -0.0740208774805069, 1.408437967300415, 2.507967710494995 } }, { { -0.0637207105755806, 1.4354344606399536, 2.4882171154022217 } }, { { -0.02519930526614189, 1.3227423429489136, 2.262523651123047 } }, { { -0.06375321745872498, 1.3159762620925903, 2.240774393081665 } }, { { -0.11614778637886047, 1.304567575454712, 2.2081098556518555 } }, { { -0.16384461522102356, 1.2939008474349976, 2.1779234409332275 } }, { { -0.21441419422626495, 1.2799254655838013, 2.146759510040283 } }, { { -0.2577652633190155, 1.269397258758545, 2.1199352741241455 } }, { { -0.3054405748844147, 1.257304072380066, 2.090388774871826 } }, { { -0.35298261046409607, 1.2458351850509644, 2.0614709854125977 } }, { { -0.3990890681743622, 1.2338658571243286, 2.030646324157715 } }, { { -0.45417800545692444, 1.2317172288894653, 1.995629072189331 } }, { { -0.4998931884765625, 1.2462588548660278, 1.96612548828125 } }, { { -0.5411544442176819, 1.2807912826538086, 1.93739652633667 } }, { { -0.5690999031066895, 1.3349567651748657, 1.9147238731384277 } }, { { -0.5748741626739502, 1.3843740224838257, 1.9062117338180542 } }, { { -0.57915860414505, 1.4185585975646973, 1.90004301071167 } }, { { 0.09219230711460114, 1.2451411485671997, 2.3451688289642334 } }, { { 0.18515899777412415, 1.2342791557312012, 2.3464181423187256 } }, { { 0.1441807597875595, 1.187577724456787, 2.3417491912841797 } }, { { 0.11663350462913513, 1.164150595664978, 2.3454487323760986 } }, { { 0.09530514478683472, 1.1364333629608154, 2.344085216522217 } }, { { 0.11537488549947739, 1.247287631034851, 2.306650161743164 } }, { { 0.1643362045288086, 1.1681218147277832, 2.32558536529541 } }, { { 0.1565936803817749, 1.1091454029083252, 2.3069679737091064 } }, { { 0.1419815868139267, 1.1385868787765503, 2.2923104763031006 } }, { { 0.11839807033538818, 1.1161680221557617, 2.27942156791687 } }, { { 0.12211541831493378, 1.2651220560073853, 2.2598073482513428 } }, { { 0.13965728878974915, 1.262778639793396, 2.1720194816589355 } }, { { 0.13534235954284668, 1.2239570617675781, 2.1224358081817627 } }, { { 0.1003403440117836, 1.2168244123458862, 2.131359815597534 } }, { { 0.07816686481237411, 1.191524624824524, 2.1217076778411865 } }, { { 0.10347070544958115, 1.3002934455871582, 2.2293972969055176 } }, { { 0.09037657827138901, 1.254019856452942, 2.1588525772094727 } }, { { 0.06675758212804794, 1.2079601287841797, 2.1226353645324707 } }, { { 0.049466345459222794, 1.1814545392990112, 2.1069953441619873 } }, { { 0.03126494213938713, 1.1533459424972534, 2.096820592880249 } }, { { 0.061368752270936966, 1.3389796018600464, 2.2364542484283447 } }, { { 0.017270343378186226, 1.308973789215088, 2.1729793548583984 } }, { { -0.014947268180549145, 1.2603204250335693, 2.1441757678985596 } }, { { -0.03415258973836899, 1.2282390594482422, 2.127288818359375 } }, { { -0.05130639299750328, 1.2003897428512573, 2.114834785461426 } }, { { 0.011532184667885303, 1.3555375337600708, 2.2826695442199707 } }, { { -0.05898936092853546, 1.3279316425323486, 2.249107837677002 } }, { { -0.10252676904201508, 1.2823903560638428, 2.2314417362213135 } }, { { -0.13047224283218384, 1.2528213262557983, 2.220081090927124 } }, { { -0.1521003097295761, 1.2267075777053833, 2.2114036083221436 } }, { { 0.06757442653179169, 1.2547895908355713, 2.381591320037842 } }, { { 0.08268353343009949, 1.3002158403396606, 2.463634729385376 } }, { { 0.057001255452632904, 1.3291199207305908, 2.5125198364257812 } }, { { 0.07843779772520065, 1.3034946918487549, 2.4991416931152344 } }, { { 0.05243687331676483, 1.285814881324768, 2.5145132541656494 } }, { { 0.02987617626786232, 1.279598355293274, 2.404419183731079 } }, { { -0.029966814443469048, 1.2421696186065674, 2.459535837173462 } }, { { -0.05352501571178436, 1.185926079750061, 2.4432289600372314 } }, { { -0.07702609896659851, 1.1575871706008911, 2.442707061767578 } }, { { -0.10007045418024063, 1.131568193435669, 2.4385986328125 } }, { { -0.002271115779876709, 1.3168889284133911, 2.3951785564422607 } }, { { -0.07002043724060059, 1.2831668853759766, 2.4347023963928223 } }, { { -0.11044945567846298, 1.2349435091018677, 2.4402732849121094 } }, { { -0.13299261033535004, 1.2077789306640625, 2.440443754196167 } }, { { -0.1531379520893097, 1.1792619228363037, 2.438023328781128 } }, { { -0.01035397034138441, 1.350235939025879, 2.348900556564331 } }, { { -0.0886465311050415, 1.3240796327590942, 2.356658697128296 } }, { { -0.1338903158903122, 1.2773090600967407, 2.3561244010925293 } }, { { -0.16112259030342102, 1.2466354370117188, 2.3569984436035156 } }, { { -0.1827191561460495, 1.219098448753357, 2.3574764728546143 } } };
    gk::Array<gk::FVector3d> positions;
    if (!positions.Reserve(89u))
        return Fail("入力位置を確保できませんでした");
    for (uint32_t index = 0; index < 89u; ++index)
        if (!positions.Append(inputValues[index]))
            return Fail("入力位置を追加できませんでした");
    gk::FVector3d input[89]{};
    memcpy(input, inputValues, sizeof(input));
    gk::String error;
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 13u, references, 76u, anchors, 115u, contacts, 35u, positions, error, 32u))
        return Fail(error.Empty() ? "複数鎖の接触fixtureがsolverに拒否されました" : error.CStr());
    if (!HasCapturedFixtureConstraints(chains, 13u, references, anchors, contacts, 35u, input, positions, error))
        return Fail("solver結果が保存後制約を満たしません");
    const gk::FVector3d witnessValues[89] = { { { 0.3778441548347473, 1.530260443687439, 2.3947529792785645 } }, { { 0.3654409052385033, 1.5110902327867382, 2.363226576761192 } }, { { 0.330080342695653, 1.5071117466807207, 2.346000854872311 } }, { { 0.2910069555675579, 1.4922990465595605, 2.328863097988905 } }, { { 0.2536636298750621, 1.4904592894726425, 2.3006677149656536 } }, { { 0.22071165872200726, 1.488438692608095, 2.265536747651074 } }, { { 0.19024346886990376, 1.4858922634608098, 2.2231685821514873 } }, { { 0.17639119343845552, 1.4796336466666395, 2.1750308413385455 } }, { { 0.13564994656590074, 1.4635187094796793, 2.147009051492888 } }, { { 0.09094484798568958, 1.464787812794839, 2.1353866114388933 } }, { { 0.06712193252113452, 1.4782824724876218, 2.1565170114281558 } }, { { 0.0572187493231166, 1.4951160004192503, 2.1855608969759643 } }, { { 0.301247239112854, 1.4959800243377686, 2.5717427730560303 } }, { { 0.27370554979976225, 1.4701159072821273, 2.58110928273181 } }, { { 0.239422634024886, 1.4617211652770714, 2.5633007948213966 } }, { { 0.19998904800244746, 1.4483170335238253, 2.545831149209337 } }, { { 0.15452819416973332, 1.4411088095303317, 2.537214229845954 } }, { { 0.10738107750366427, 1.4311699751906526, 2.5387892227009052 } }, { { 0.05784755835812977, 1.4178297446488661, 2.548705048272955 } }, { { 0.018960290287466264, 1.3948551100284887, 2.5712492176936217 } }, { { -0.027533984536672884, 1.3775392463481608, 2.5556544976480944 } }, { { -0.06498370699647726, 1.38255723885089, 2.5290543928570557 } }, { { -0.074020937895861, 1.4084379460151912, 2.507967689638404 } }, { { -0.0637207645357965, 1.4354344562734942, 2.4882170821678873 } }, { { -0.02519930526614189, 1.3227423429489136, 2.262523651123047 } }, { { -0.06375318934049876, 1.3159762670272437, 2.2407744089438832 } }, { { -0.11614783151046243, 1.3045675644395376, 2.2081098258476657 } }, { { -0.16384463393373688, 1.2939008423280383, 2.177923427849049 } }, { { -0.2144141708849379, 1.2799254720988742, 2.146759522872263 } }, { { -0.2577652678984599, 1.2693972584928015, 2.119935269679657 } }, { { -0.3054405381320907, 1.2573040825984234, 2.090388796042463 } }, { { -0.3529826355248248, 1.2458351803577403, 2.061470968984995 } }, { { -0.39908906182688864, 1.2338658605847364, 2.0306463287281944 } }, { { -0.4541779921478403, 1.231717232621396, 1.9956290811849544 } }, { { -0.49989322339699754, 1.2462588739393745, 1.9661254661505916 } }, { { -0.5411544164060114, 1.2807912492253137, 1.9373965478844755 } }, { { -0.5690998754191541, 1.3349567319869766, 1.9147238945855203 } }, { { -0.5748741384550924, 1.3843740189815652, 1.906211750151787 } }, { { -0.5791585819217251, 1.418558609984335, 1.9000430241722421 } }, { { 0.09219230711460114, 1.2451411485671997, 2.3451688289642334 } }, { { 0.18543551089553859, 1.2497913022054712, 2.3519827785988534 } }, { { 0.1860863699757617, 1.1888741815898856, 2.338916943638805 } }, { { 0.1536875316883829, 1.1735466401560437, 2.344978962625308 } }, { { 0.13235918440718758, 1.1458294237756432, 2.343615447574454 } }, { { 0.11537488549947739, 1.247287631034851, 2.306650161743164 } }, { { 0.16628563508520883, 1.1687921191035089, 2.3230677280559693 } }, { { 0.15397772354375378, 1.10801405652822, 2.316797870467945 } }, { { 0.13839963514484063, 1.13715337032498, 2.302536856664055 } }, { { 0.11372988659930093, 1.1125006626537992, 2.299612099237882 } }, { { 0.12211541831493378, 1.2651220560073853, 2.2598073482513428 } }, { { 0.14242002463257, 1.2665586565567237, 2.1725974021921473 } }, { { 0.13977405642541468, 1.2288931564746588, 2.1220150660890127 } }, { { 0.10481606125547903, 1.2224678084295284, 2.1316226103064957 } }, { { 0.08264254590855431, 1.1971679796640995, 2.1219704568150983 } }, { { 0.10347070544958115, 1.3002934455871582, 2.2293972969055176 } }, { { 0.09037657373143088, 1.254019840409096, 2.15885255275041 } }, { { 0.06675758574160931, 1.2079601286406239, 2.122635352575961 } }, { { 0.04946637372232929, 1.1814545769405331, 2.1069953545011293 } }, { { 0.03126494388296594, 1.1533459391844216, 2.0968205883947317 } }, { { 0.061368752270936966, 1.3389796018600464, 2.2364542484283447 } }, { { 0.017270309564862765, 1.3089737662075402, 2.172979306187774 } }, { { -0.014947255580811752, 1.2603204721166164, 2.1441757607227125 } }, { { -0.03415263861455797, 1.2282390038393523, 2.1272887571286736 } }, { { -0.051306370129979656, 1.2003898037187744, 2.1148347763182143 } }, { { 0.011532184667885303, 1.3555375337600708, 2.2826695442199707 } }, { { -0.05898935815622343, 1.3279316436175794, 2.249107838996365 } }, { { -0.10252676764594965, 1.2823903557094831, 2.231441736982239 } }, { { -0.1304722321216286, 1.252821335757075, 2.2200810954746597 } }, { { -0.15210032111855276, 1.2267075605236641, 2.2114036040030385 } }, { { 0.06757442653179169, 1.2547895908355713, 2.381591320037842 } }, { { 0.09204069958597172, 1.3061638255263806, 2.457650912741656 } }, { { 0.03536843995754685, 1.3083284302601943, 2.483503282107588 } }, { { 0.07047255748155279, 1.3004890599237686, 2.4823197186309565 } }, { { 0.04290912074366271, 1.2847878253793215, 2.497106149530229 } }, { { 0.02987617626786232, 1.279598355293274, 2.404419183731079 } }, { { -0.031078143328799795, 1.2521822122031299, 2.4640251444315338 } }, { { -0.06322824288766005, 1.1982718759888005, 2.45736954769264 } }, { { -0.08929955928803544, 1.1725353196139268, 2.461054608746417 } }, { { -0.11234392257577924, 1.1465163333323782, 2.45694617835186 } }, { { -0.002271115779876709, 1.3168889284133911, 2.3951785564422607 } }, { { -0.07051330675354069, 1.285295546730497, 2.435603061368061 } }, { { -0.11094232245239069, 1.2370721737234578, 2.4411739495099276 } }, { { -0.13348546207923134, 1.20990761339691, 2.4413444186803295 } }, { { -0.1536307960757386, 1.1813906165503165, 2.4389239941973337 } }, { { -0.01035397034138441, 1.350235939025879, 2.348900556564331 } }, { { -0.08864653980336762, 1.324079629853121, 2.3566586979902278 } }, { { -0.13389031478554234, 1.277309067324677, 2.356124402070228 } }, { { -0.16112259100883383, 1.2466354422007193, 2.3569984446393137 } }, { { -0.18271917204407895, 1.2190984345708096, 2.3574764742266936 } } };
    gk::Array<gk::FVector3d> witness;
    if (!witness.Reserve(89u))
        return Fail("witness位置を確保できませんでした");
    for (uint32_t index = 0; index < 89u; ++index)
        if (!witness.Append(witnessValues[index]))
            return Fail("witness位置を追加できませんでした");
    gk::FVector3d witnessInput[89]{};
    memcpy(witnessInput, witnessValues, sizeof(witnessInput));
    if (!HasCapturedFixtureConstraints(chains, 13u, references, anchors, contacts, 35u, witnessInput, witness, error))
        return Fail("witnessが保存後制約を満たしません");
    if (!ResolvePreparedSecondaryMotionWeightedContacts(chains, 13u, references, 76u, anchors, 115u, contacts, 35u, witness, error, 32u))
        return Fail(error.Empty() ? "solverが成立witnessを拒否しました" : error.CStr());
    if (memcmp(witnessInput, witness.Data(), sizeof(witnessInput)) != 0 || !HasCapturedFixtureConstraints(chains, 13u, references, anchors, contacts, 35u, witnessInput, witness, error))
        return Fail("solverが成立witnessを保持できません");
    return true;
}

/**
 * 最初のclear姿勢が、独立に確認した近い成立姿勢から離れ過ぎる場合を検出する。
 */
bool TestWeightedFirstClearIsNotNearestClearCandidate()
{
    using namespace gk::model::animation;
    // 鎖の線分に近接する二つの身体球を保持する。
    const FPreparedSecondaryMotionCollisionShape bodyShapes[2] = { { { { 1.0084617859641316, 0.05349957128263816, 0.19684669728837578 } }, { { 1.0084617859641316, 0.05349957128263816, 0.19684669728837578 } }, { { 0.0, 0.0, 0.0 } }, 0.0, 0.0, 0.37291932331261435, 0.00037291932331261435, false, 0.0, { 0.6355424626515173, -0.3194197520299762, -0.17607262602423857 }, { 1.381381109276746, 0.4264188945952525, 0.5697660206009901 }, { 0.6355424626515171, -0.31941975202997624, -0.1760726260242386 }, { 1.3813811092767463, 0.42641889459525256, 0.5697660206009902 } }, { { { 1.1053682157136526, -0.042423842369294695, -0.24146540544431183 } }, { { 1.1053682157136526, -0.042423842369294695, -0.24146540544431183 } }, { { 0.0, 0.0, 0.0 } }, 0.0, 0.0, 0.306629234654599, 0.000306629234654599, false, 0.0, { 0.7987389810590536, -0.3490530770238937, -0.5480946400989108 }, { 1.4119974503682515, 0.2642053922853043, 0.06516382921028718 }, { 0.7987389810590535, -0.34905307702389377, -0.548094640098911 }, { 1.4119974503682517, 0.2642053922853044, 0.0651638292102872 } } };
    // weighted contact経路も通すが、身体球から離れた固定形状。
    const FPreparedSecondaryMotionCollisionShape distantContactShape = { { { 10.0, 0.0, 0.0 } }, { { 10.0, 0.0, 0.0 } }, { { 0.0, 0.0, 0.0 } }, 0.0, 0.0, 0.1, 0.0001, false, 0.0, { 9.9, -0.1, -0.1 }, { 10.1, 0.1, 0.1 }, { 9.899999999999999, -0.10000000000000002, -0.10000000000000002 }, { 10.100000000000001, 0.10000000000000002, 0.10000000000000002 } };
    FSecondaryMotionContactChain chain{};
    chain.firstPoint = 0u;
    chain.firstSegment = 0u;
    chain.segmentCount = 1u;
    chain.cosineLimit = -1.0;
    chain.shapes = bodyShapes;
    chain.shapeCount = 2u;
    const FSecondaryMotionReferenceSegment reference = { { { 1.0, 0.0, 0.0 } }, 1.0 };
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0u;
    anchor.segmentIndex = 0u;
    anchor.definition.direction = { { 1.0, 0.0, 0.0 } };
    anchor.definition.offset = { { 1.0, 0.0, 0.0 } };
    anchor.definition.cosineLimit = -1.0;
    anchor.weight = 1.0;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.firstAnchor = 0u;
    contact.anchorCount = 1u;
    contact.shapes = &distantContactShape;
    contact.shapeCount = 1u;
    const gk::FVector3d inputValues[2] = { { { 0.0, 0.0, 0.0 } }, { { 1.0, 0.0, 0.0 } } };
    gk::Array<gk::FVector3d> positions;
    if (!positions.Reserve(2u) || !positions.Append(inputValues[0]) || !positions.Append(inputValues[1]))
        return Fail("初期位置を確保できませんでした");
    const gk::FVector3d witnessValues[2] = { { { 0.0, 0.0, 0.0 } }, { { 0.9663899781345132, -0.2558367713285378, -0.02525780270262068 } } };
    gk::Array<gk::FVector3d> witness;
    if (!witness.Reserve(2u) || !witness.Append(witnessValues[0]) || !witness.Append(witnessValues[1]))
        return Fail("独立witnessを確保できませんでした");
    gk::FVector3d input[2]{};
    memcpy(input, inputValues, sizeof(input));
    gk::FVector3d witnessInput[2]{};
    memcpy(witnessInput, witnessValues, sizeof(witnessInput));
    gk::String error;
    const double witnessDistance = Distance(inputValues[1], witness.At(1));
    // 保存後の全制約を満たした成立姿勢の移動量に0.001 model unitの余裕を加える。
    const double distanceBound = witnessDistance + 0.001;
    if (witnessDistance > 0.27 || !HasCapturedFixtureConstraints(&chain, 1u, &reference, &anchor, &contact, 1u, input, witness, error))
        return Fail("独立witnessが距離上限または保存後制約を満たしません");
    if (!ResolvePreparedSecondaryMotionWeightedContacts(&chain, 1u, &reference, 1u, &anchor, 1u, &contact, 1u, positions, error, 32u))
        return Fail(error.Empty() ? "weighted solverがclear姿勢を返しませんでした" : error.CStr());
    if (!HasCapturedFixtureConstraints(&chain, 1u, &reference, &anchor, &contact, 1u, input, positions, error))
        return Fail("solver姿勢がfloat保存後の全制約を満たしません");
    const double firstClearDistance = Distance(inputValues[1], positions.At(1));
    if (firstClearDistance > distanceBound)
        return Fail("最初のclear seedが近い成立姿勢を見落としました");
    return true;
}

}

int main()
{
    return TestWeightedFirstClearIsNotNearestClearCandidate() && TestCurrentInputContactContinuationAndStoredWitness() && TestContactContinuationSeedAndStoredWitness() && TestCapturedWeightedContactCluster() && TestOpposedAnchorsUseTheirWeightedSkinPosition(0.8f) && TestOpposedAnchorsUseTheirWeightedSkinPosition(0.99f) && TestUnrelatedChainKeepsItsCurrentDirectionDuringWeightedSeeding() && TestTinyBodyContactDoesNotOvercorrect() && TestRotatedCurrentPoseKeepsNearbyClearCorrection() && TestBodyDirectionPrefersNearbyClearPoseToClearReference() && TestThreeAnchorsAndInvalidInputsAreAtomic() && TestAntiparallelInitialDirectionFindsClearSeed() && TestNearAntiparallelWeightedContactUsesSeedFallback() && TestRootZeroOffsetAndFixedOnlyContacts() && TestRootHeavyWeightedOffsetsWithFixedHip() && TestMultiSegmentWeightedOffsetsUseAncestorRotation() && TestWeightedSpheresCompeteWithChainBodyCapsules() ? 0 : 1;
}
