To build this project together with the dependencies on Windows execute in the project's folder:
(there i sno need to build the NativeAotLib separately)

`dotnet msbuild -target:BuildWithExternalDependencies -property:Configuration=Release`

resulting console app + dependencies will be in ConsoleWrapper\bin\Release\net8.0\ folder