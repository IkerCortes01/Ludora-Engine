// Modulo de la aplicacion.

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.ludora.apk"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.ludora.apk"

        // minSdk 26 (Android 8.0): es la primera version con AndroidKeyStore
        // capaz de exigir autenticacion del usuario por clave, que es sobre lo
        // que se apoya el desbloqueo biometrico de Ludora. Bajar de aqui
        // obligaria a inventar un camino inseguro para los telefonos viejos.
        minSdk = 26
        targetSdk = 35

        versionCode = 1
        versionName = "0.1-cimientos"

        // No hay pruebas instrumentadas todavia; cuando las haya, este es el
        // runner que toca.
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            // Sin ofuscar mientras el proyecto son cimientos: un stack trace
            // legible vale mas ahora que unos kilobytes de menos.
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)

    val composeBom = platform(libs.androidx.compose.bom)
    implementation(composeBom)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)

    implementation(libs.androidx.datastore.preferences)
    implementation(libs.androidx.biometric)
    implementation(libs.androidx.fragment.ktx)

    debugImplementation(libs.androidx.compose.ui.tooling)
}
