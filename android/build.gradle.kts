// Archivo raiz: aqui solo se DECLARAN los plugins, sin aplicarlos.
// Cada modulo (de momento solo `:app`) aplica los que necesita.

plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.compose) apply false
}
