#include "ObjLoader.h"
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

/**
 * Internal conversion from bounded OBJ text into retained model payloads.
 */
namespace gk::detail {
/**
 * Token parsing and checked array operations used only for static OBJ input.
 */
namespace {
const uint32_t maxSourceValues = 2000000u;
const uint32_t maxOutputVertices = 4000000u;
const uint32_t maxOutputIndices = 6000000u;

/**
 * A source texture coordinate stored before face expansion.
 */
struct ObjVec2 { float value[2]; };
/**
 * A source position or normal stored before face expansion.
 */
struct ObjVec3 { float value[3]; };
/**
 * One OBJ face corner using signed source indices.
 */
struct ObjIndex { int32_t position; int32_t uv; int32_t normal; };

/**
 * Rejects non-finite source values before they enter model geometry.
 */
bool IsFinite(float value) { return isfinite(value) != 0; }

/**
 * Advances over horizontal whitespace accepted between OBJ tokens.
 */
void SkipSpace(const char*& cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r') ++cursor;
}
/**
 * Parses a finite bounded coordinate and advances the token cursor.
 */
bool ReadFloat(const char*& cursor, float& value) {
    SkipSpace(cursor);
    char* end = nullptr;
    value = strtof(cursor, &end);
    if (end == cursor || !IsFinite(value) || fabsf(value) > 1000000.0f) return false;
    cursor = end;
    return true;
}
/**
 * Parses a signed 32-bit OBJ index without accepting overflow.
 */
bool ReadInt32(const char*& cursor, int32_t& value) {
    char* end = nullptr;
    errno = 0;
    const long parsed = strtol(cursor, &end, 10);
    if (end == cursor || errno == ERANGE || parsed < INT32_MIN || parsed > INT32_MAX) return false;
    value = static_cast<int32_t>(parsed);
    cursor = end;
    return true;
}
/**
 * Reads the position/UV/normal index triplet for one face corner.
 */
bool ReadObjIndex(const char*& cursor, ObjIndex& index) {
    if (!ReadInt32(cursor, index.position) || index.position == 0) return false;
    index.uv = 0;
    index.normal = 0;
    if (*cursor == '/') {
        ++cursor;
        if (*cursor != '/') {
            if (!ReadInt32(cursor, index.uv) || index.uv == 0) return false;
        }
        if (*cursor == '/') {
            ++cursor;
            if (!ReadInt32(cursor, index.normal) || index.normal == 0) return false;
        }
    }
    return *cursor == '\0' || *cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '#';
}
/**
 * Resolves a one-based positive or relative negative OBJ index to an array offset.
 */
bool ResolveObjIndex(int32_t source, uint32_t count, uint32_t& result) {
    const int64_t resolved = source > 0 ? static_cast<int64_t>(source) - 1 : static_cast<int64_t>(count) + source;
    if (resolved < 0 || static_cast<uint64_t>(resolved) >= count) return false;
    result = static_cast<uint32_t>(resolved);
    return true;
}
/**
 * Appends one expanded face vertex while enforcing the output geometry limit.
 */
bool AppendModelVertex(ModelResource& model, const ModelVertex& vertex, uint32_t& index, String& error) {
    if (model.vertices.Count() >= maxOutputVertices) {
        error.Assign("model has too many vertices");
        return false;
    }
    index = model.vertices.Count();
    if (!model.vertices.Append(vertex)) {
        error.Assign("model vertex allocation failed");
        return false;
    }
    return true;
}
/**
 * Appends one checked model index while enforcing the output geometry limit.
 */
bool AppendModelIndex(ModelResource& model, uint32_t index, String& error) {
    if (model.indices.Count() >= maxOutputIndices || !model.indices.Append(index)) {
        error.Assign("model index allocation failed or model has too many indices");
        return false;
    }
    return true;
}

/**
 * Parses supported OBJ records and triangulates polygon faces into the model payload.
 */
bool ParseObj(const uint8_t* bytes, uint32_t size, ModelResource& model, String& error) {
    Array<ObjVec3> positions;
    Array<ObjVec3> normals;
    Array<ObjVec2> uvs;
    Array<ObjIndex> face;
    uint32_t lineNumber = 0;
    uint32_t offset = 0;
    String line;
    while (offset < size) {
        const uint32_t lineStart = offset;
        while (offset < size && bytes[offset] != '\n') ++offset;
        const uint32_t lineLength = offset - lineStart;
        if (offset < size) ++offset;
        ++lineNumber;
        if (lineLength > 1024u * 1024u) {
            error.Assign("OBJ line exceeds the parser limit");
            return false;
        }
        if (!line.Assign(reinterpret_cast<const char*>(bytes + lineStart), lineLength)) {
            error.Assign("OBJ line allocation failed");
            return false;
        }
        const char* cursor = line.CStr();
        SkipSpace(cursor);
        if (*cursor == '\0' || *cursor == '#') continue;
        if (cursor[0] == 'v' && (cursor[1] == ' ' || cursor[1] == '\t')) {
            ObjVec3 value{};
            ++cursor;
            if (!ReadFloat(cursor, value.value[0]) || !ReadFloat(cursor, value.value[1]) || !ReadFloat(cursor, value.value[2]) ||
                positions.Count() >= maxSourceValues || !positions.Append(value)) {
                error.Assign("invalid or excessive OBJ position at line "); error.AppendUnsigned(lineNumber); return false;
            }
        } else if (cursor[0] == 'v' && cursor[1] == 'n' && (cursor[2] == ' ' || cursor[2] == '\t')) {
            ObjVec3 value{};
            cursor += 2;
            if (!ReadFloat(cursor, value.value[0]) || !ReadFloat(cursor, value.value[1]) || !ReadFloat(cursor, value.value[2]) ||
                normals.Count() >= maxSourceValues || !normals.Append(value)) {
                error.Assign("invalid or excessive OBJ normal at line "); error.AppendUnsigned(lineNumber); return false;
            }
        } else if (cursor[0] == 'v' && cursor[1] == 't' && (cursor[2] == ' ' || cursor[2] == '\t')) {
            ObjVec2 value{};
            cursor += 2;
            if (!ReadFloat(cursor, value.value[0]) || !ReadFloat(cursor, value.value[1]) ||
                uvs.Count() >= maxSourceValues || !uvs.Append(value)) {
                error.Assign("invalid or excessive OBJ texture coordinate at line "); error.AppendUnsigned(lineNumber); return false;
            }
        } else if (cursor[0] == 'f' && (cursor[1] == ' ' || cursor[1] == '\t')) {
            cursor++;
            face.Clear();
            while (true) {
                SkipSpace(cursor);
                if (*cursor == '\0' || *cursor == '#') break;
                ObjIndex index{};
                uint32_t resolved = 0;
                if (!ReadObjIndex(cursor, index) || !ResolveObjIndex(index.position, positions.Count(), resolved) ||
                    (index.uv && !ResolveObjIndex(index.uv, uvs.Count(), resolved)) ||
                    (index.normal && !ResolveObjIndex(index.normal, normals.Count(), resolved)) || !face.Append(index)) {
                    error.Assign("invalid OBJ face index at line "); error.AppendUnsigned(lineNumber); return false;
                }
            }
            if (face.Count() < 3) {
                error.Assign("OBJ face has fewer than three vertices at line "); error.AppendUnsigned(lineNumber); return false;
            }
            const uint32_t firstIndex = model.indices.Count();
            for (uint32_t triangle = 1; triangle + 1 < face.Count(); ++triangle) {
                const ObjIndex corner[3] = {face.At(0), face.At(triangle), face.At(triangle + 1)};
                ModelVertex vertices[3]{};
                for (uint32_t c = 0; c < 3; ++c) {
                    uint32_t positionIndex = 0;
                    ResolveObjIndex(corner[c].position, positions.Count(), positionIndex);
                    for (uint32_t axis = 0; axis < 3; ++axis) vertices[c].position[axis] = positions.At(positionIndex).value[axis];
                    if (corner[c].uv) {
                        uint32_t uvIndex = 0;
                        ResolveObjIndex(corner[c].uv, uvs.Count(), uvIndex);
                        vertices[c].uv[0] = uvs.At(uvIndex).value[0];
                        vertices[c].uv[1] = uvs.At(uvIndex).value[1];
                    }
                    if (corner[c].normal) {
                        uint32_t normalIndex = 0;
                        ResolveObjIndex(corner[c].normal, normals.Count(), normalIndex);
                        for (uint32_t axis = 0; axis < 3; ++axis) vertices[c].normal[axis] = normals.At(normalIndex).value[axis];
                    }
                }
                const float ax = vertices[1].position[0] - vertices[0].position[0];
                const float ay = vertices[1].position[1] - vertices[0].position[1];
                const float az = vertices[1].position[2] - vertices[0].position[2];
                const float bx = vertices[2].position[0] - vertices[0].position[0];
                const float by = vertices[2].position[1] - vertices[0].position[1];
                const float bz = vertices[2].position[2] - vertices[0].position[2];
                float generated[3] = {ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx};
                const float length = sqrtf(generated[0]*generated[0] + generated[1]*generated[1] + generated[2]*generated[2]);
                if (length > 0.0f) for (uint32_t axis = 0; axis < 3; ++axis) generated[axis] /= length;
                for (uint32_t c = 0; c < 3; ++c) {
                    if (!corner[c].normal) for (uint32_t axis = 0; axis < 3; ++axis) vertices[c].normal[axis] = generated[axis];
                    uint32_t vertexIndex = 0;
                    if (!AppendModelVertex(model, vertices[c], vertexIndex, error) || !AppendModelIndex(model, vertexIndex, error)) return false;
                }
            }
            ModelPrimitive primitive = {firstIndex, model.indices.Count() - firstIndex, 0};
            if (!model.primitives.Append(primitive)) { error.Assign("model primitive allocation failed"); return false; }
        }
    }
    if (!model.indices.Count()) { error.Assign("OBJ contains no faces"); return false; }
    return true;
}


}

/**
 * Internal entry point called by the format-neutral model loader.
 */
bool LoadObjPayload(const uint8_t* bytes, uint32_t size, ModelResource& model, String& error) {
    return ParseObj(bytes, size, model, error);
}
}
