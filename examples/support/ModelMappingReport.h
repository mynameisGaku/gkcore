// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_EXAMPLES_MODELMAPPINGREPORT_H
#define GKCORE_EXAMPLES_MODELMAPPINGREPORT_H

#include <gkcore/ModelAnimation.h>
#include <stddef.h>

/**
 * 外部motionの適用結果を表示用の固定長文字列へまとめる。
 */
namespace gk::examples
{

/**
 * 対応数、画面表示、未対応役割を保持する値。
 */
struct FModelMappingReport
{
    // 登録済み対応の数。
    FModelAnimationMappingInfo info{};
    // 同じ役割で結び付かなかった人型役割の数。
    uint32_t missingRoleCount = 0;
    // 未対応役割があるかを示す。
    bool hasMissingRoles = false;
    // UIへ表示する短い要約。
    char summary[160]{};
    // 不足役割を人が読める形にした文字列。
    char missingRoles[512]{};
};

/**
 * 登録済みmapping情報を取得し、UI要約と不足役割一覧を作る。
 * API照会に失敗した場合はfalseを返す。
 */
bool BuildModelMappingReport(gk::ModelHandle model, uint32_t slot, const char* label, FModelMappingReport& report);

/**
 * mappingの件数と不足役割を標準出力へ一度だけ記録する。
 */
void PrintModelMappingReport(const char* tag, uint32_t modelRoleCount, uint32_t mappedBoneCount, const FModelMappingReport& report);

}

#endif
