// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>

#include <cstdio>
#include <cstring>

/**
 * JSON文字列用に診断内容の引用符と改行をescapeする。
 */
void PrintJsonString(const char* value)
{
    std::putchar('"');
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value ? value : ""); *cursor; ++cursor)
    {
        if (*cursor == '"' || *cursor == '\\')
        {
            std::putchar('\\');
            std::putchar(*cursor);
        }
        else if (*cursor == '\n')
        {
            std::fputs("\\n", stdout);
        }
        else if (*cursor == '\r')
        {
            std::fputs("\\r", stdout);
        }
        else if (*cursor < 0x20)
        {
            std::fputs("?", stdout);
        }
        else
        {
            std::putchar(*cursor);
        }
    }
    std::putchar('"');
}

/**
 * 失敗段階とAPI診断を機械判定可能な一行JSONで出力する。
 */
void PrintFailure(const char* stage, const char* diagnostic)
{
    std::fputs("{\"failure_stage\":", stdout);
    PrintJsonString(stage);
    std::fputs(",\"diagnostic\":", stdout);
    PrintJsonString(diagnostic);
    std::fputs("}\n", stdout);
}

int main(int argc, char** argv)
{
    const bool expectSuccess = argc == 3 && std::strcmp(argv[2], "--expect-success") == 0;
    if ((argc != 2 && argc != 3) || (argc == 3 && !expectSuccess))
    {
        std::fprintf(stderr, "usage: model_gpu_skinning_error_tests <fixture.fbx> [--expect-success]\n");
        return 2;
    }

    bool initialized = false;
    bool passed = false;
    gk::ModelHandle model{};
    do
    {
        if (gk::SetWindowSize(640, 480) != 0 || gk::Init() != 0)
        {
            PrintFailure("init", gk::GetLastErrorMessage());
            break;
        }
        initialized = true;
        model = gk::LoadModel(argv[1]);
        if (!model.IsValid())
        {
            PrintFailure("load", gk::GetLastErrorMessage());
            break;
        }
        if (gk::GetModelAnimationCount(model) == 0)
        {
            PrintFailure("fixture", "loaded model has no embedded animation clip");
            break;
        }

        // 初期姿勢の三角形を実際に描き、入力fixtureの基礎状態を確かめる。
        if (gk::BeginFrame() != 0 || gk::DrawModel(model) != 0 || gk::Present() != 0)
        {
            PrintFailure("rest", gk::GetLastErrorMessage());
            break;
        }
        if (gk::PlayModelAnimation(model, 0, false) != 0 || gk::SetModelAnimationTime(model, 0.5) != 0)
        {
            PrintFailure("pose", gk::GetLastErrorMessage());
            break;
        }
        if (gk::BeginFrame() != 0)
        {
            PrintFailure("begin_frame", gk::GetLastErrorMessage());
            break;
        }
        if (gk::DrawModel(model) != 0)
        {
            // CPU skinning fallbackは退化入力をDraw時に拒否できる。
            const char* diagnostic = gk::GetLastErrorMessage();
            if (!diagnostic || diagnostic[0] == '\0')
            {
                PrintFailure("draw", "DrawModel failed without a diagnostic");
                break;
            }
            PrintFailure("draw", diagnostic);
            passed = !expectSuccess;
            break;
        }
        if (gk::Present() != 0)
        {
            const char* diagnostic = gk::GetLastErrorMessage();
            if (!diagnostic || diagnostic[0] == '\0')
            {
                PrintFailure("present", "Present failed without a diagnostic");
                break;
            }
            PrintFailure("present", diagnostic);
            passed = !expectSuccess;
            break;
        }
        if (expectSuccess)
        {
            std::puts("{\"result\":\"passed\",\"expect_success\":true}");
            passed = true;
        }
        else
        {
            PrintFailure("unexpected_success", "invalid GPU skinning pose was accepted by DrawModel and Present");
        }
    } while (false);

    if (model.IsValid())
    {
        gk::DeleteModel(model);
    }
    if (initialized)
    {
        gk::Shutdown();
    }
    return passed ? 0 : 1;
}
