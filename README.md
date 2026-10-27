# Sakura Editor — 改造版（2026-06-sakura）

**このリポジトリは、Windows用テキストエディタ [サクラエディタ（sakura-editor/sakura）](https://github.com/sakura-editor/sakura) をフォークし、独自の機能追加・改修を行っている派生版です。本家の公式リポジトリ・公式配布版ではありません。**

- 派生元：[sakura-editor/sakura](https://github.com/sakura-editor/sakura)
- この改造版：[doss-eeic/2026-06-sakura](https://github.com/doss-eeic/2026-06-sakura)
- ライセンス：[zlib License](./LICENSE)。元のサクラエディタの著作権表示・ライセンス通知を引き継いでいます。

[![License: Zlib](https://img.shields.io/badge/License-Zlib-lightgrey.svg)](https://opensource.org/licenses/Zlib)

<!-- 以下は Markdownの参照形式によるリンク の定義です。 -->
<!-- 参照 https://hail2u.net/blog/coding/markdown-reference-style-links.html -->

[Visual Studio 以前のバージョン]: https://visualstudio.microsoft.com/ja/vs/older-downloads/ "Visual Studio 以前のバージョン"
[Visual Studio 最新版]: https://visualstudio.microsoft.com/ja/downloads/ "Visual Studio 最新版"
[ライセンスの OSI のページ]: https://opensource.org/license/zlib
[Visual Studio Community ライセンス]: https://visualstudio.microsoft.com/ja/license-terms/vs2022-ga-community/
[Markdown をローカルで確認する方法]: https://github.com/sakura-editor/sakura/wiki/markdown-%E3%82%92%E3%83%AD%E3%83%BC%E3%82%AB%E3%83%AB%E3%81%A7%E7%A2%BA%E8%AA%8D%E3%81%99%E3%82%8B%E6%96%B9%E6%B3%95
[How to extract currently installed Visual Studio component IDs?]: https://stackoverflow.com/questions/52946333/how-to-extract-currently-installed-visual-studio-component-ids
[Configure Visual Studio across your organization with .vsconfig]: https://devblogs.microsoft.com/setup/configure-visual-studio-across-your-organization-with-vsconfig/
[インストール構成をインポートまたはエクスポートする]: https://docs.microsoft.com/ja-jp/visualstudio/install/import-export-installation-configurations?view=vs-2019
[コマンド ライン パラメーターを使用して Visual Studio をインストールする]: https://docs.microsoft.com/ja-jp/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2019
[不足しているコンポーネントを自動的にインストールする]: https://docs.microsoft.com/ja-jp/visualstudio/install/import-export-installation-configurations?view=vs-2019#automatically-install-missing-components

<!-- TOC -->

- [Sakura Editor — 改造版（2026-06-sakura）](#sakura-editor--改造版2026-06-sakura)
  - [この改造版の主な変更](#この改造版の主な変更)
  - [入手・ビルド](#入手ビルド)
  - [Markdown編集・タイプ別ショートカット](#markdown編集タイプ別ショートカット)
  - [開発情報](#開発情報)
    - [How to build](#how-to-build)
    - [Static code analysis](#static-code-analysis)
  - [変更履歴](#変更履歴)
  - [不具合報告・提案](#不具合報告提案)
  - [本家の情報](#本家の情報)

<!-- /TOC -->

## この改造版の主な変更

- Markdownの編集コマンド、装飾表示、入力補助、見出しアウトライン。
- タイプ別のショートカット設定と、構文表示・スマートインデントの個別設定。
- 太字・斜体・取り消し線の部分選択・部分解除、および複数行選択への対応。
- 画像リンクの挿入・ドロップ・ホバープレビュー、本文中に画像を表示するノート表示、画像一覧のアウトライン。
- 検索履歴をすべて削除する機能。

各変更の経緯と詳細は、このリポジトリの[Pull Requests](https://github.com/doss-eeic/2026-06-sakura/pulls?q=is%3Apr)を参照してください。

## 入手・ビルド

この改造版を利用する場合は、[このリポジトリのソース](https://github.com/doss-eeic/2026-06-sakura)を取得し、以下のビルド手順を参照してください。CIの実行状況・成果物は[このリポジトリのGitHub Actions](https://github.com/doss-eeic/2026-06-sakura/actions)で確認できます。

本家の配布版は [Sakura Editor Portal](https://sakura-editor.github.io/) から入手できます。本家の配布物には、このリポジトリ独自の変更は含まれません。

## Markdown編集・タイプ別ショートカット

[設定方法、初期キー割り当て、編集動作](./help/markdown-editing.md)を参照してください。

画像関連の説明は[画像リンクのプレビュー](./tools/image-link-preview.md)と[ノート表示](./tools/note-view.md)を参照してください。

## 開発情報

### How to build

`sakura.sln`を開いてビルドできます。  
詳細は [ビルド方法](./tools/build.md) を参照。

### Static code analysis

本家サクラエディタで利用されている静的解析サービスです。以下のリンク先の結果は本家のものであり、この改造版の解析結果を示すものではありません。

| サービス | 説明 |
|---------|------|
| [CodeFactor](https://www.codefactor.io/repository/github/sakura-editor/sakura) |  C++ のスタイルチェック。[cpplint.py](./tools/CodeFactor/README.md)による字句解析。 |
| [SonarQube](https://sonarcloud.io/summary/new_code?id=sakura-editor_sakura&branch=master) | 多言語対応の静的解析。Build Wrapper + SonarScannerによるコード品質解析。 |

## 変更履歴

- この改造版の変更：[コミット履歴](https://github.com/doss-eeic/2026-06-sakura/commits/main/)・[Pull Requests](https://github.com/doss-eeic/2026-06-sakura/pulls?q=is%3Apr)。
- 本家から引き継いだ変更履歴：[CHANGELOG.md](./CHANGELOG.md)。この改造版の追加変更を網羅するものではありません。

## 不具合報告・提案

この改造版に関する通常の不具合報告・機能提案は、[このリポジトリのIssues](https://github.com/doss-eeic/2026-06-sakura/issues)へお願いします。

## 本家の情報

- [公式サイト・公式配布版](https://sakura-editor.github.io/)
- [公式リポジトリ](https://github.com/sakura-editor/sakura)
- [本家の最新の変更履歴](https://github.com/sakura-editor/sakura/blob/master/CHANGELOG.md)
- [本家に対する脆弱性の報告窓口](https://github.com/sakura-editor/sakura/security/advisories)
