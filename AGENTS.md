# Repository working rules

## Branches and commits

- Develop only on `dev`. Promote reviewed, tested, stable, useful checkpoints from `dev` to `main`; do not wait for the entire SDK to be finished.
- The root agent owns commits and pushes so parallel work does not race.
- 使い終わった一時ファイル・調査用プログラム・重複した検証buildは作業後に片づける。ユーザーの元データ、現在のbuild、必要な最新検証記録は保持し、削除前に対象がworkspace内の生成物であることを確認する。
- Keep each small, verified change in a `dev` commit and push it to the remote `dev` branch. Promote stable, reviewed checkpoints from `dev` to `main`.
- Write commit messages in Japanese using one of these forms:
  - `feat (x): 〇〇のため、△△を追加`
  - `fix (x): 〇〇のため、△△を修正`
  - `asset (x): 〇〇を追加/修正`
  - `refactor (x): 〇〇のため、△△を変更`
  - `docs (x): ドキュメント追加`
  - `other (x): 〇〇`

## Implementation and review

- Use tests-first development: capture a failing contract test, implement the smallest change, then rerun focused and relevant integration tests.
- Keep Runtime SDK contents limited to files required to build and run a downstream game. Keep source, test tools, compiler tools, samples, and development dependencies in the development checkout/package.
- Do not use the C++ standard library or STL in authored Runtime code under `include/`, `src/`, or user-facing `examples/`. Tests and development tools may use it.
- Keep Runtime responsibilities separated into focused, paired headers and implementation files: public API/lifecycle and events, drawing/backend, resources, and effects/shaders. Keep tests and build tools outside Runtime code. Shared Runtime handles, strings, vectors, and containers belong to the foundation owner; do not add local one-off container implementations in feature modules.
- Do not claim a feature or platform test passed unless it was run on the required platform. Record hardware, toolchain, command, and result for native GPU tests.
- The root agent reviews integration and owns final status updates. Parallel workers use the available `gpt-6-luna` model at low reasoning effort when delegation is requested; do not claim `luna6.1` is available.
- Agents should edit only their assigned files and report cross-area issues to the root agent.
- Keep the README user-facing and concise. Do not use milestone/phase labels in product docs; describe supported capabilities and measurable final acceptance criteria instead.

## Source and comments

- 追加・変更するコードコメントは自然な日本語にする。example、バージョン、ビルドなど、一般的な技術用語を無理に訳さない。
- 追加・変更する自作ソースは UTF-8 BOM 付き、改行は CRLF にする。固定バージョンの依存物には形式変更を加えない。
- C++ の型・関数・制御文・lambda の開始中カッコは次の行に置く。短い処理もブロックを1行に畳まない。括弧と初期化子の内部は1行を維持する。
- Keep comments useful: explain a non-obvious invariant or public usage; do not narrate trivial code.
- Do not add external product comparisons or attribution in source comments. Required vendor attribution belongs in license/notice files.
- Document public C++ types, functions, namespaces, and enums with this block-comment form:

  ```cpp
  /**
   * Brief description.
   */
  ```

- Use `//` comments for local variables and code blocks when a comment is needed.
