import * as fs from 'fs';
import type {
    CmakeMinimumRequiredV10,
    Schema
} from "./schema"

type CMakeSupportedVersion = 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10;
type CMakeCompiler = "msvc" | "msvc-clang" | "gcc" | "gcc-cross" | "clang" | "clang-cross";
type CMakeBuildType = "Debug" | "Release" | "RelWithDebInfo" | "MinSizeRel";
type CMakeTarget = "all" | "install" | "package";

const CMAKE_MINIMUM_REQUIRED: CmakeMinimumRequiredV10 = {
    major: 3,
    minor: 15
};

const CMAKE_DEFAULT_CONFIGURATION_PRESET = {
    name: "default",
    hidden: true,
    binaryDir: "${sourceDir}/build"
}

const CMAKE_DEFAULT_MSVC_CONFIGURATION_PRESET = [
    generateDefaultConfigurationPreset("msvc"),
    generateDefaultConfigurationPreset("msvc-clang")
]

const CMAKE_DEFAULT_GCC_CONFIGURATION_PRESET = [
    generateDefaultConfigurationPreset("gcc"),
    generateDefaultConfigurationPreset("gcc", "Release"),
]

const CMAKE_DEFAULT_CLANG_CONFIGURATION_PRESET = [
    generateDefaultConfigurationPreset("clang"),
    generateDefaultConfigurationPreset("clang", "Release"),
]

function generateDefaultConfigurationPreset(compiler: CMakeCompiler, buildType?: CMakeBuildType) {
    const result: {
        name: string,
        hidden?: boolean,
        inherits?: string[],
        generator?: string,
        environment?: Record<string, string>,
        cacheVariables?: Record<string, string>
    } = { name: `default-${compiler}`, hidden: true };
    result.inherits = ["default"];
    result.generator = "Ninja Multi-Config";

    if (compiler === "msvc" || compiler === "msvc-clang") {
        result.generator = "Visual Studio 17 2022";
    } else {
        result.environment = {
            CC: `${compiler}`,
            CXX: `${compiler === "gcc" ? "g++" : "clang++"}`
        };
    }

    if (buildType !== undefined) {
        result.name = `${result.name}-${buildType.toLowerCase()}`;
        result.inherits[0] = `${result.inherits[0]}-${compiler}`
        result.generator = "Ninja";
        delete result.environment;
        result.cacheVariables = { CMAKE_BUILD_TYPE: buildType };
    }

    return result;
}

const CMAKE_DEFAULT_WINDOWS_CONFIGURATION_PRESET = [
    generateWindowsConfigurationPreset("msvc"),
    generateWindowsConfigurationPreset("msvc", "all"),
    generateWindowsConfigurationPreset("msvc", "install"),
    generateWindowsConfigurationPreset("msvc", "package"),

    generateWindowsConfigurationPreset("msvc-clang"),
    generateWindowsConfigurationPreset("msvc-clang", "all"),
    generateWindowsConfigurationPreset("msvc-clang", "install"),
    generateWindowsConfigurationPreset("msvc-clang", "package"),

    generateWindowsConfigurationPreset("gcc"),
    generateWindowsConfigurationPreset("gcc", "all"),
    generateWindowsConfigurationPreset("gcc", "install"),
    generateWindowsConfigurationPreset("gcc", "package"),

    generateWindowsConfigurationPreset("clang"),
    generateWindowsConfigurationPreset("clang", "all"),
    generateWindowsConfigurationPreset("clang", "install"),
    generateWindowsConfigurationPreset("clang", "package"),
]

function generateWindowsConfigurationPreset(compiler: CMakeCompiler, target?: CMakeTarget) {
    const result: {
        name: string,
        inherits?: string[],
        installDir?: string,
        cacheVariables?: Record<string, string>,
        architecture?: string,
        toolset?: string,
    } = { name: `windows-x64-${compiler}` };

    result.inherits = [`default-${compiler}`];
    result.installDir = "${sourceDir}/build";
    result.cacheVariables = {
        ARCH: "native",
        SET_PACKAGE_OUTPUT_SUFFIX: "windows-x64-msvc"
    };

    if (compiler === "msvc" || compiler === "msvc-clang") {
        result.architecture = "x64";

        if (compiler === "msvc-clang") {
            result.toolset = "ClangCL,host=x64";
        } else {
            result.toolset = "host=x64";
        }
    } else {
        result.name = `windows-x64-mingw-${compiler}`;
        result.cacheVariables.SET_PACKAGE_OUTPUT_SUFFIX = `windows-x64-mingw-${compiler}`;
    }

    if (target !== undefined) {
        result.name = `${result.name}-${target}`;

        if (compiler === "gcc" || compiler === "clang") {
            result.inherits[0] = `${result.inherits[0]}-release`;
        }
    }

    if (target === "package") {
        result.cacheVariables.ARCH = "default";
        result.cacheVariables.SET_COMMIT_ID_IN_VERSION = "OFF";
    }

    return result;
}

const CMAKE_DEFAULT_LINUX_CONFIGURATION_PRESET = [
    generateLinuxConfigurationPreset("gcc", "x64"),
    generateLinuxConfigurationPreset("gcc", "x64", "all"),
    generateLinuxConfigurationPreset("gcc", "x64", "install"),
    generateLinuxConfigurationPreset("gcc", "x64", "package"),

    generateLinuxConfigurationPreset("gcc", "amd64"),
    generateLinuxConfigurationPreset("gcc", "amd64", "all"),
    generateLinuxConfigurationPreset("gcc", "amd64", "install"),
    generateLinuxConfigurationPreset("gcc", "amd64", "package"),

    generateLinuxConfigurationPreset("gcc", "arm64"),
    generateLinuxConfigurationPreset("gcc", "arm64", "all"),
    generateLinuxConfigurationPreset("gcc", "arm64", "install"),
    generateLinuxConfigurationPreset("gcc", "arm64", "package"),

    generateLinuxConfigurationPreset("gcc-cross", "arm64", undefined),
    generateLinuxConfigurationPreset("gcc-cross", "arm64", "all"),
    generateLinuxConfigurationPreset("gcc-cross", "arm64", "package"),

    generateLinuxConfigurationPreset("clang", "x64"),
    generateLinuxConfigurationPreset("clang", "x64", "all"),
    generateLinuxConfigurationPreset("clang", "x64", "install"),
    generateLinuxConfigurationPreset("clang", "x64", "package"),

    generateLinuxConfigurationPreset("clang", "amd64"),
    generateLinuxConfigurationPreset("clang", "amd64", "all"),
    generateLinuxConfigurationPreset("clang", "amd64", "install"),
    generateLinuxConfigurationPreset("clang", "amd64", "package"),

    generateLinuxConfigurationPreset("clang", "arm64"),
    generateLinuxConfigurationPreset("clang", "arm64", "all"),
    generateLinuxConfigurationPreset("clang", "arm64", "install"),
    generateLinuxConfigurationPreset("clang", "arm64", "package"),

    generateLinuxConfigurationPreset("clang-cross", "arm64", undefined),
    generateLinuxConfigurationPreset("clang-cross", "arm64", "all"),
    generateLinuxConfigurationPreset("clang-cross", "arm64", "package"),
];

function generateLinuxConfigurationPreset(compiler: CMakeCompiler, arch: "x64" | "amd64" | "arm64", target?: CMakeTarget) {
    const result: {
        name: string,
        inherits?: string[],
        installDir?: string,
        environment?: Record<string, string>,
        cacheVariables?: Record<string, string>,
    } = { name: `linux-${arch}-${compiler}` };
    
    result.inherits = [`default-${compiler}`];
    result.cacheVariables = {
        ARCH: "native",
        SET_PACKAGE_OUTPUT_SUFFIX: `linux-${arch}-${compiler}`
    };

    if (arch === "amd64") {
    }

    if (compiler === "gcc-cross" || compiler === "clang-cross") {
        if (compiler === "gcc-cross") {
            result.inherits[0] = "default-gcc";
            result.environment = {
                CC: "aarch64-linux-gnu-gcc",
                CXX: "aarch64-linux-gnu-g++"
            };
            result.cacheVariables.CMAKE_TOOLCHAIN_FILE = `\${sourceDir}/CMake/linux-${arch}-gcc.cmake`;
        } else {
            result.inherits[0] = "default-clang";
            result.cacheVariables.CMAKE_TOOLCHAIN_FILE = `\${sourceDir}/CMake/linux-${arch}-clang.cmake`;
        }

        result.cacheVariables.ARCH = "default";
    }

    if (target !== undefined) {
        result.name = `${result.name}-${target}`;
        result.inherits[0] = `${result.inherits[0]}-release`;
    }

    if (target === "package") {
        result.cacheVariables.ARCH = "default";
        result.cacheVariables.SET_COMMIT_ID_IN_VERSION = "OFF";
    }

    if (compiler === "clang" || compiler === "clang-cross") {
    }

    return result;
}

const CMAKE_DEFAULT_MACOS_CONFIGURATION_PRESET = [
    generateMacOSConfigurationPreset("clang", "x64"),
    generateMacOSConfigurationPreset("clang", "x64", "all"),
    generateMacOSConfigurationPreset("clang", "x64", "install"),
    generateMacOSConfigurationPreset("clang", "x64", "package"),
];

function generateMacOSConfigurationPreset(compiler: CMakeCompiler, arch: "x64", target?: CMakeTarget) {
    const result: {
        name: string,
        inherits?: string[],
        environment?: Record<string, string>,
        cacheVariables?: Record<string, string>,
    } = { name: `osx-${arch}-${compiler}` };

    result.inherits = [`default-${compiler}`];
    result.environment = {
        PATH: "/usr/local/opt/llvm/bin:$penv{PATH}",
        LDFLAGS: "-L/usr/local/opt/llvm/lib/c++ -L/usr/local/opt/llvm/lib -lunwind",
        CPPFLAGS: "-I/usr/local/opt/llvm/include"
    }
    result.cacheVariables = {
        ARCH: "native",
        SET_PACKAGE_OUTPUT_SUFFIX: `osx-${arch}-${compiler}`
    }

    if (target !== undefined) {
        result.name = `${result.name}-${target}`;
        result.inherits[0] = `${result.inherits[0]}-release`;
    }

    if (target === "package") {
        result.cacheVariables.ARCH = "default";
        result.cacheVariables.SET_COMMIT_ID_IN_VERSION = "OFF";
    }

    return result;
}

function generateDefaultBuildPreset(os: "windows" | "linux" | "osx", arch: "x64" | "amd64" | "arm64", compiler: CMakeCompiler, target?: CMakeTarget, buildType?: CMakeBuildType) {
    const result: {
        name: string,
        configurePreset: string,
        configuration?: string,
        targets?: string[]
    } = { 
        name: `${os}-${arch}-${compiler}`,
        configurePreset: `${os}-${arch}-${compiler}`,
    };

    if (os === "windows" && compiler !== "msvc" && compiler !== "msvc-clang") {
        result.name = `${os}-${arch}-mingw-${compiler}`;
        result.configurePreset = `${os}-${arch}-mingw-${compiler}`;
    }

    if (target !== undefined) {
        result.name = `${result.name}-${target}`;
        result.configurePreset = `${result.configurePreset}-${target}`;
        result.targets = [
            os === "windows" && compiler === "msvc" || compiler === "msvc-clang" 
            ? target.toUpperCase() 
            : target
        ];

        if (os === "windows" && (compiler === "msvc" || compiler === "msvc-clang")) {
            result.configuration = "Release";
        }
    }

    if (buildType !== undefined) {
        result.name = `${result.name}-${buildType.toLowerCase()}`;
        result.configuration = buildType;
    }

    return result;
}

function generatePresetConfig(version: CMakeSupportedVersion): Schema {
    return {
        version: version,
        cmakeMinimumRequired: CMAKE_MINIMUM_REQUIRED,
        configurePresets: [
            CMAKE_DEFAULT_CONFIGURATION_PRESET,
            ...CMAKE_DEFAULT_MSVC_CONFIGURATION_PRESET,
            ...CMAKE_DEFAULT_GCC_CONFIGURATION_PRESET,
            ...CMAKE_DEFAULT_CLANG_CONFIGURATION_PRESET,
            ...CMAKE_DEFAULT_WINDOWS_CONFIGURATION_PRESET,
            ...CMAKE_DEFAULT_LINUX_CONFIGURATION_PRESET,
            ...CMAKE_DEFAULT_MACOS_CONFIGURATION_PRESET
        ],
        buildPresets: [
            generateDefaultBuildPreset("windows", "x64", "msvc", undefined, "Debug"),
            generateDefaultBuildPreset("windows", "x64", "msvc", undefined, "Release"),
            generateDefaultBuildPreset("windows", "x64", "msvc", "all"),
            generateDefaultBuildPreset("windows", "x64", "msvc", "install"),
            generateDefaultBuildPreset("windows", "x64", "msvc", "package"),

            generateDefaultBuildPreset("windows", "x64", "msvc-clang", undefined, "Debug"),
            generateDefaultBuildPreset("windows", "x64", "msvc-clang", undefined, "Release"),
            generateDefaultBuildPreset("windows", "x64", "msvc-clang", "all"),
            generateDefaultBuildPreset("windows", "x64", "msvc-clang", "install"),
            generateDefaultBuildPreset("windows", "x64", "msvc-clang", "package"),

            generateDefaultBuildPreset("windows", "x64", "gcc", undefined, "Debug"),
            generateDefaultBuildPreset("windows", "x64", "gcc", undefined, "Release"),
            generateDefaultBuildPreset("windows", "x64", "gcc", "all"),
            generateDefaultBuildPreset("windows", "x64", "gcc", "install"),
            generateDefaultBuildPreset("windows", "x64", "gcc", "package"),

            generateDefaultBuildPreset("windows", "x64", "clang", undefined, "Debug"),
            generateDefaultBuildPreset("windows", "x64", "clang", undefined, "Release"),
            generateDefaultBuildPreset("windows", "x64", "clang", "all"),
            generateDefaultBuildPreset("windows", "x64", "clang", "install"),
            generateDefaultBuildPreset("windows", "x64", "clang", "package"),

            generateDefaultBuildPreset("linux", "x64", "gcc", undefined, "Debug"),
            generateDefaultBuildPreset("linux", "x64", "gcc", undefined, "Release"),
            generateDefaultBuildPreset("linux", "x64", "gcc", "all"),
            generateDefaultBuildPreset("linux", "x64", "gcc", "install"),
            generateDefaultBuildPreset("linux", "x64", "gcc", "package"),

            generateDefaultBuildPreset("linux", "x64", "clang", undefined, "Debug"),
            generateDefaultBuildPreset("linux", "x64", "clang", undefined, "Release"),
            generateDefaultBuildPreset("linux", "x64", "clang", "all"),
            generateDefaultBuildPreset("linux", "x64", "clang", "install"),
            generateDefaultBuildPreset("linux", "x64", "clang", "package"),

            generateDefaultBuildPreset("linux", "amd64", "gcc", undefined, "Debug"),
            generateDefaultBuildPreset("linux", "amd64", "gcc", undefined, "Release"),
            generateDefaultBuildPreset("linux", "amd64", "gcc", "all"),
            generateDefaultBuildPreset("linux", "amd64", "gcc", "install"),
            generateDefaultBuildPreset("linux", "amd64", "gcc", "package"),

            generateDefaultBuildPreset("linux", "amd64", "clang", undefined, "Debug"),
            generateDefaultBuildPreset("linux", "amd64", "clang", undefined, "Release"),
            generateDefaultBuildPreset("linux", "amd64", "clang", "all"),
            generateDefaultBuildPreset("linux", "amd64", "clang", "install"),
            generateDefaultBuildPreset("linux", "amd64", "clang", "package"),

            generateDefaultBuildPreset("linux", "arm64", "gcc", undefined, "Debug"),
            generateDefaultBuildPreset("linux", "arm64", "gcc", undefined, "Release"),
            generateDefaultBuildPreset("linux", "arm64", "gcc", "all"),
            generateDefaultBuildPreset("linux", "arm64", "gcc", "install"),
            generateDefaultBuildPreset("linux", "arm64", "gcc", "package"),

            generateDefaultBuildPreset("linux", "arm64", "clang", undefined, "Debug"),
            generateDefaultBuildPreset("linux", "arm64", "clang", undefined, "Release"),
            generateDefaultBuildPreset("linux", "arm64", "clang", "all"),
            generateDefaultBuildPreset("linux", "arm64", "clang", "install"),
            generateDefaultBuildPreset("linux", "arm64", "clang", "package"),

            generateDefaultBuildPreset("linux", "arm64", "gcc-cross", undefined, "Debug"),
            generateDefaultBuildPreset("linux", "arm64", "gcc-cross", undefined, "Release"),
            generateDefaultBuildPreset("linux", "arm64", "gcc-cross", "all"),
            generateDefaultBuildPreset("linux", "arm64", "gcc-cross", "package"),

            generateDefaultBuildPreset("linux", "arm64", "clang-cross", undefined, "Debug"),
            generateDefaultBuildPreset("linux", "arm64", "clang-cross", undefined, "Release"),
            generateDefaultBuildPreset("linux", "arm64", "clang-cross", "all"),
            generateDefaultBuildPreset("linux", "arm64", "clang-cross", "package"),

            generateDefaultBuildPreset("osx", "x64", "clang", undefined, "Debug"),
            generateDefaultBuildPreset("osx", "x64", "clang", undefined, "Release"),
            generateDefaultBuildPreset("osx", "x64", "clang", "all"),
            generateDefaultBuildPreset("osx", "x64", "clang", "install"),
            generateDefaultBuildPreset("osx", "x64", "clang", "package"),
        ]
    }
}

fs.writeFileSync("CMakePresets.json", JSON.stringify(generatePresetConfig(3), undefined, 4));
