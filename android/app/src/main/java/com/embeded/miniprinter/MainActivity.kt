package com.embeded.miniprinter

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.embeded.miniprinter.ui.PrinterScreen

class MainActivity : ComponentActivity() {
    private lateinit var model: PrinterViewModel

    private val permissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { grants ->
        if (grants.values.all { it }) discoverPrinters()
        else model.showError("Allow Nearby devices to discover printers.")
    }
    private val enableBluetooth = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        val adapter = (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        if (adapter?.isEnabled == true) model.scan()
        else model.showError("Turn on Bluetooth to discover printers.")
    }
    private val pickPhoto = registerForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) model.loadPhoto(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        model = ViewModelProvider(this)[PrinterViewModel::class.java]
        setContent {
            val state = model.state.collectAsStateWithLifecycle().value
            PrinterScreen(
                state = state,
                onScan = ::discoverPrinters,
                onConnect = model::connect,
                onDisconnect = model::disconnect,
                onChoosePhoto = {
                    pickPhoto.launch(
                        PickVisualMediaRequest(
                            ActivityResultContracts.PickVisualMedia.ImageOnly,
                        ),
                    )
                },
                onDensityChange = model::setDensity,
                onPrintPhoto = model::printPhoto,
                onPrintTestPage = model::printTestPage,
                onDismissError = model::clearError,
            )
        }
        if (savedInstanceState == null) discoverPrinters()
    }

    @SuppressLint("MissingPermission") // Permission checks precede Bluetooth adapter access.
    private fun discoverPrinters() {
        val required = if (Build.VERSION.SDK_INT >= 31) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val missing = required.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            permissions.launch(missing.toTypedArray())
            return
        }

        val adapter = (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        if (adapter?.isEnabled != true) {
            enableBluetooth.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            return
        }
        model.scan()
    }
}
