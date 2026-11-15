// Configuracion del proyecto Gradle de Ludora para telefono.
//
// Un solo modulo (`:app`). No hay modulos de libreria porque todavia no hay
// nada que compartir: cuando exista un cliente de red o un lector de paquetes
// .tomate, esos si merecen modulo propio.

pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    // Los modulos no declaran repositorios por su cuenta: todos salen de aqui.
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "Ludora"
include(":app")
