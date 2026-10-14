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
    if (positions.Count() != 20u)
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
}

int main()
{
    return TestCapturedWeightedContactCluster() && TestOpposedAnchorsUseTheirWeightedSkinPosition(0.8f) && TestOpposedAnchorsUseTheirWeightedSkinPosition(0.99f) && TestUnrelatedChainKeepsItsCurrentDirectionDuringWeightedSeeding() && TestTinyBodyContactDoesNotOvercorrect() && TestRotatedCurrentPoseKeepsNearbyClearCorrection() && TestBodyDirectionPrefersNearbyClearPoseToClearReference() && TestThreeAnchorsAndInvalidInputsAreAtomic() && TestAntiparallelInitialDirectionFindsClearSeed() && TestNearAntiparallelWeightedContactUsesSeedFallback() && TestRootZeroOffsetAndFixedOnlyContacts() && TestRootHeavyWeightedOffsetsWithFixedHip() && TestMultiSegmentWeightedOffsetsUseAncestorRotation() && TestWeightedSpheresCompeteWithChainBodyCapsules() ? 0 : 1;
}
