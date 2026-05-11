# Repository Guidelines

## Project Structure & Module Organization
This repository is a StackChan monorepo. `app/` contains the Flutter mobile client (`lib/`, `assets/`, `test/`). `server/` contains the Go backend (`api/`, `internal/`, `manifest/`, `utility/`). `firmware/` holds the ESP-IDF robot firmware, with code under `main/` and hardware support under `main/hal/`. `remote/` contains the ESP-NOW remote controller firmware, with code in `remote/code/main/`.

## Build, Test, and Development Commands
Run commands from the relevant component directory.

- `cd app && flutter pub get`: install Flutter dependencies.
- `cd app && flutter analyze`: run Dart and Flutter static analysis.
- `cd app && flutter test`: run widget and unit tests.
- `cd app && flutter run -d ios` or `flutter run -d android`: launch the mobile app.
- `cd server && go run main.go`: start the backend locally.
- `cd server && go test ./...`: run Go tests.
- `cd server && make build`: build with GoFrame tooling.
- `cd firmware && python3 ./fetch_repos.py && idf.py build`: fetch ESP-IDF dependencies and build firmware.
- `cd remote/code && idf.py build`: build the remote controller firmware.

## Coding Style & Naming Conventions
Flutter follows `flutter_lints`; keep files in `snake_case.dart`, classes in `UpperCamelCase`, and prefer small widgets under `app/lib/view/`. Go code should stay `gofmt`-clean, with lowercase package names and feature folders grouped by API or service area. Firmware and remote C/C++ use the checked-in `.clang-format` files: 4-space indentation, 120-column limit, no tabs. Preserve existing directory naming such as `app_*`, `hal_*`, and `v1`/`v2` API namespaces.

## Testing Guidelines
Add tests next to the component you change: Flutter tests in `app/test/`, Go tests as `*_test.go` beside the package under test. Run the narrowest relevant suite first, then rerun the full local suite for that component. There is no dedicated firmware test harness here, so firmware and remote changes should include `idf.py build` verification.

## Commit & Pull Request Guidelines
Recent history favors short, imperative subjects with an optional scope, for example `feat(server): add v2 device API` or `update firmware v1.2.4`. Keep commits focused by component. PRs should explain the changed area, list validation commands, link issues when applicable, and include screenshots or recordings for `app/` UI changes.

## Security & Configuration Tips
Do not commit live secrets or environment-specific endpoints. Review `server/manifest/config/config.yaml`, `app/lib/network/urls.dart`, and `app/lib/util/value_constant.dart` before building locally, and keep signing keys, JWT secrets, and RSA material out of version control.
