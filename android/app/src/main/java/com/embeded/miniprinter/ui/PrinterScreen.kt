package com.embeded.miniprinter.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import android.os.Build
import com.embeded.miniprinter.PrinterUiState
import com.embeded.miniprinter.bluetooth.DiscoveredPrinter
import kotlin.math.roundToInt

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PrinterScreen(
    state: PrinterUiState,
    onScan: () -> Unit,
    onConnect: (DiscoveredPrinter) -> Unit,
    onDisconnect: () -> Unit,
    onChoosePhoto: () -> Unit,
    onDensityChange: (Int) -> Unit,
    onPrintPhoto: () -> Unit,
    onPrintTestPage: () -> Unit,
    onDismissError: () -> Unit,
) {
    val context = LocalContext.current
    val dark = isSystemInDarkTheme()
    val colors = when {
        Build.VERSION.SDK_INT >= 31 && dark -> dynamicDarkColorScheme(context)
        Build.VERSION.SDK_INT >= 31 -> dynamicLightColorScheme(context)
        dark -> darkColorScheme()
        else -> lightColorScheme()
    }

    MaterialTheme(colorScheme = colors) {
        Scaffold(topBar = { TopAppBar(title = { Text("Mini Printer") }) }) { padding ->
            Column(
                modifier = Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState())
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                SectionCard("Printer") {
                    if (state.connectedName != null) {
                        StatusRow("Connected to", state.connectedName)
                        state.status?.let { status ->
                            StatusRow("State", status.state.title)
                            StatusRow("Battery", "${status.battery}%")
                            StatusRow("Print head", status.temperature?.let { "$it°C" } ?: "Unavailable")
                            StatusRow("Paper", if (status.paperPresent) "Ready" else "Out of paper")
                            status.errorMessage?.let { StatusRow("Error", it) }
                        }
                        TextButton(onClick = onDisconnect, enabled = !state.printing) { Text("Disconnect") }
                    } else {
                        if (state.devices.isEmpty()) {
                            Text(
                                when {
                                    state.scanning -> "Searching nearby…"
                                    !state.bluetoothAvailable -> "Turn on Bluetooth to discover printers."
                                    else -> "Tap Scan to find your printer."
                                },
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        state.devices.forEach { device ->
                            val connecting = state.connectingAddress == device.address
                            ListItem(
                                headlineContent = { Text(device.name) },
                                supportingContent = {
                                    Text(if (connecting) "Connecting…" else "Signal ${device.rssi} dBm")
                                },
                                trailingContent = {
                                    if (connecting) CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 2.dp)
                                    else Text("Connect", color = MaterialTheme.colorScheme.primary)
                                },
                                modifier = Modifier.fillMaxWidth()
                                    .clickable(enabled = state.connectingAddress == null) { onConnect(device) },
                            )
                        }
                        OutlinedButton(
                            onClick = onScan,
                            enabled = state.connectingAddress == null,
                        ) { Text(if (state.scanning) "Scan Again" else "Scan for Printers") }
                    }
                }

                SectionCard("Photo") {
                    OutlinedButton(onClick = onChoosePhoto) { Text("Choose Photo") }
                    state.photo?.let { photo ->
                        Image(
                            bitmap = photo.asImageBitmap(),
                            contentDescription = "Selected photo preview",
                            contentScale = ContentScale.Fit,
                            modifier = Modifier.fillMaxWidth().height(240.dp),
                        )
                    } ?: Text(
                        "Select a photo to print it in black and white.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }

                SectionCard("Print") {
                    StatusRow("Density", "${state.density}%")
                    Slider(
                        value = state.density.toFloat(),
                        onValueChange = { onDensityChange(it.roundToInt()) },
                        valueRange = 1f..100f,
                        steps = 98,
                    )
                    val canPrint = state.connectedName != null && !state.printing &&
                        state.status?.paperPresent != false
                    Button(onClick = onPrintPhoto, enabled = canPrint && state.photo != null) {
                        Text("Print Photo")
                    }
                    OutlinedButton(onClick = onPrintTestPage, enabled = canPrint) {
                        Text("Print Test Page")
                    }
                    if (state.printing) {
                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                        StatusRow("Submitted lines", state.submittedLines.toString())
                    }
                }
            }
        }

        state.error?.let { message ->
            AlertDialog(
                onDismissRequest = onDismissError,
                title = { Text("Printer Error") },
                text = { Text(message) },
                confirmButton = { TextButton(onClick = onDismissError) { Text("OK") } },
            )
        }
    }
}

@Composable
private fun SectionCard(title: String, content: @Composable () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            HorizontalDivider()
            content()
        }
    }
}

@Composable
private fun StatusRow(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.Top) {
        Text(
            label,
            modifier = Modifier.weight(1f),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.width(12.dp))
        Text(value, modifier = Modifier.weight(1f), textAlign = TextAlign.End)
    }
}
