// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>

#include <cstdio>

namespace
{
/**
 * 診断文字列をJSON用にescapeしてstderrへ出す。
 */
void PrintJsonString(const char* value)
{
    std::fputc('"', stderr);
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value ? value : ""); *cursor; ++cursor)
    {
        if (*cursor == '"' || *cursor == '\\')
        {
            std::fputc('\\', stderr);
            std::fputc(*cursor, stderr);
        }
        else if (*cursor == '\n')
        {
            std::fputs("\\n", stderr);
        }
        else if (*cursor == '\r')
        {
            std::fputs("\\r", stderr);
        }
        else if (*cursor < 0x20)
        {
            std::fputc('?', stderr);
        }
        else
        {
            std::fputc(*cursor, stderr);
        }
    }
    std::fputc('"', stderr);
}

/**
 * 操作名とAPI診断を一行JSONで出力する。
 */
void PrintFailure(const char* stage, const char* diagnostic)
{
    std::fputs("{\"failure_stage\":", stderr);
    PrintJsonString(stage);
    std::fputs(",\"diagnostic\":", stderr);
    PrintJsonString(diagnostic);
    std::fputs("}\n", stderr);
}

/**
 * 公開APIの失敗を診断付きで報告する。
 */
bool Check(int result, const char* stage)
{
    if (result == 0)
    {
        return true;
    }
    PrintFailure(stage, gk::GetLastErrorMessage());
    return false;
}
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: model_gpu_skinning_multi_tests <fixture-directory>\n");
        return 2;
    }

    bool initialized = false;
    bool passed = false;
    gk::ModelHandle translationModel{};
    gk::ModelHandle transformedModel{};
    do
    {
        if (!Check(gk::SetWindowSize(640, 480), "set_window_size") || !Check(gk::Init(), "init"))
        {
            break;
        }
        initialized = true;

        char translationPath[4096]{};
        char transformedPath[4096]{};
        const int translationPathLength = std::snprintf(translationPath, sizeof(translationPath), "%s/fbx-animation-skin-translation.fbx", argv[1]);
        const int transformedPathLength = std::snprintf(transformedPath, sizeof(transformedPath), "%s/fbx-animation-skin-transformed.fbx", argv[1]);
        if (translationPathLength <= 0 || static_cast<size_t>(translationPathLength) >= sizeof(translationPath) || transformedPathLength <= 0 || static_cast<size_t>(transformedPathLength) >= sizeof(transformedPath))
        {
            PrintFailure("fixture_path", "fixture directory path exceeded its bound");
            break;
        }
        translationModel = gk::LoadModel(translationPath);
        if (!translationModel.IsValid())
        {
            PrintFailure("load_translation", gk::GetLastErrorMessage());
            break;
        }
        transformedModel = gk::LoadModel(transformedPath);
        if (!transformedModel.IsValid())
        {
            PrintFailure("load_transformed", gk::GetLastErrorMessage());
            break;
        }
        if (gk::GetModelAnimationCount(translationModel) == 0 || gk::GetModelAnimationCount(transformedModel) == 0)
        {
            PrintFailure("fixture_animation", "one of the skin fixtures has no embedded clip");
            break;
        }
        if (!Check(gk::PlayModelAnimation(translationModel, 0, false), "play_translation") || !Check(gk::SetModelAnimationTime(translationModel, 0.25), "time_translation") || !Check(gk::PlayModelAnimation(transformedModel, 0, false), "play_transformed") || !Check(gk::SetModelAnimationTime(transformedModel, 0.75), "time_transformed"))
        {
            break;
        }

        bool framesPassed = true;
        for (uint32_t frame = 0; frame < 8 && framesPassed; ++frame)
        {
            if (!Check(gk::BeginFrame(), "begin_frame"))
            {
                framesPassed = false;
                break;
            }
            // 描画順を毎frame反転し、複数dispatchの順序依存を調べる。
            if ((frame & 1u) == 0)
            {
                framesPassed = Check(gk::DrawModel(translationModel), "draw_translation") && Check(gk::DrawModel(transformedModel), "draw_transformed");
            }
            else
            {
                framesPassed = Check(gk::DrawModel(transformedModel), "draw_transformed") && Check(gk::DrawModel(translationModel), "draw_translation");
            }
            if (!framesPassed)
            {
                break;
            }
            if (frame == 7)
            {
                // 最終frameの予約済みdrawがhandle解放後もPresentできることを確かめる。
                if (!Check(gk::DeleteModel(transformedModel), "delete_transformed_before_present"))
                {
                    framesPassed = false;
                    break;
                }
                transformedModel = {};
                if (!Check(gk::DeleteModel(translationModel), "delete_translation_before_present"))
                {
                    framesPassed = false;
                    break;
                }
                translationModel = {};
            }
            if (!Check(gk::Present(), "present"))
            {
                framesPassed = false;
                break;
            }
        }
        if (framesPassed)
        {
            std::puts("{\"frames\":8,\"draws_per_frame\":2,\"result\":\"passed\"}");
            passed = true;
        }
    } while (false);

    if (transformedModel.IsValid())
    {
        gk::DeleteModel(transformedModel);
    }
    if (translationModel.IsValid())
    {
        gk::DeleteModel(translationModel);
    }
    if (initialized)
    {
        gk::Shutdown();
    }
    return passed ? 0 : 1;
}
