import QtQuick
import QtQuick.Window
import QtTest
import Main

TestCase {
    name: "AppSegfaultTest"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 800
        visible: true
        title: "Photon Test"
        
        RawViewport {
            id: viewport
            anchors.fill: parent
        }
    }

    function test_closeWhileProcessing() {
        // Simulate load and process
        viewport.setSource(AppState.currentImage);
        wait(500); // Wait for load
        
        // Start heavy denoise
        viewport.setDenoiseEnabled(true);
        viewport.setDenoiseAmount(50.0);
        viewport.startAsyncDenoise(true, 1.0);
        
        wait(100); // Let it start
        
        // Close window immediately
        testWindow.close();
        
        // Wait a bit to see if it crashes
        wait(1000);
        verify(true); // If we get here, no segfault
    }
}
