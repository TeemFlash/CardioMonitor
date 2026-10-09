plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("com.google.devtools.ksp")
}

kotlin {
    jvmToolchain(17)
}

android {
    namespace = "com.cardiomonitor.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.cardiomonitor.app"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0-beta"
    }

    buildFeatures {
        compose = true
    }
    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.15"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    // Compose
    implementation(platform("androidx.compose:compose-bom:2024.06.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.activity:activity-compose:1.9.1")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.4")
    implementation("androidx.navigation:navigation-compose:2.7.7")

    // Room + SQLCipher (шифрованное локальное хранилище истории анализов)
    implementation("androidx.room:room-runtime:2.6.1")
    implementation("androidx.room:room-ktx:2.6.1")
    ksp("androidx.room:room-compiler:2.6.1")
    implementation("net.zetetic:android-database-sqlcipher:4.5.4")
    implementation("androidx.sqlite:sqlite:2.4.0")

    // Биометрическая блокировка доступа к истории
    implementation("androidx.biometric:biometric:1.1.0")

    // Material Components for Material 3 themes
    implementation("com.google.android.material:material:1.12.0")

    // Coroutines для сетевого слоя
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")

    // Графики ЭКГ
    implementation("com.github.PhilJay:MPAndroidChart:v3.1.0")
}
