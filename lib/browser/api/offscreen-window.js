'use strict';

const electron = require('electron');
const {
  OffscreenHostView,
  WebContentsView,
  BrowserWindow,
  TopLevelWindow,
  webContents
} = electron;

const { EventEmitter } = require('events');
const { OffscreenWindow } = process.electronBinding('offscreen_window');

Object.setPrototypeOf(OffscreenWindow.prototype, EventEmitter.prototype);

class OffscreenHostWindow extends BrowserWindow {
  constructor () {
    if (OffscreenHostWindow._instance) {
      return OffscreenHostWindow._instance;
    }

    super({
      show: false,
      width: 0,
      height: 0,
      resizable: false,
      fullscreenable: false,
      fullscreen: false
    });

    this._init();

    OffscreenHostWindow._instance = this;
    return this;
  }

  static instance () {
    return new OffscreenHostWindow();
  }

  _init () {
    // Call parent class's _init.
    TopLevelWindow.prototype._init.call(this);

    this._hostView = new OffscreenHostView();
    // Create WebContentsView.
    this.setContentView(this._hostView);
  }

  addWebContents (webContentsView) {
    this._hostView.addWebContents(webContentsView);
  }

  removeWebContents (id) {
    this._hostView.removeWebContents(id);
    if (this._hostView.childCount === 0) {
      OffscreenHostWindow._instance = null;
      this.close();
    }
  }
}

OffscreenWindow.prototype._init = function () {
  // Avoid recursive require.
  const { app } = electron;

  // Create WebContentsView.
  OffscreenHostWindow.instance().addWebContents(
    new WebContentsView(this.webContents));

  this.setOwnerWindow(OffscreenHostWindow.instance());

  // Change window title to page title.
  this.webContents.on('page-title-updated', (event, title, ...args) => {
    // Route the event to BrowserWindow.
    this.emit('page-title-updated', event, title, ...args);
  });

  // Sometimes the webContents doesn't get focus when window is shown, so we
  // have to force focusing on webContents in this case. The safest way is to
  // focus it when we first start to load URL, if we do it earlier it won't
  // have effect, if we do it later we might move focus in the page.
  //
  // Though this hack is only needed on macOS when the app is launched from
  // Finder, we still do it on all platforms in case of other bugs we don't
  // know.
  this.webContents.once('load-url', function () {
    this.focus();
  });

  // Redirect focus/blur event to app instance too.
  this.on('blur', (event) => {
    app.emit('offscreen-window-blur', event, this);
  });
  this.on('focus', (event) => {
    app.emit('offscreen-window-focus', event, this);
  });
  this.on('closed', () => {
    OffscreenHostWindow.instance().removeWebContents(this.webContents.id);
  });

  // Notify the creation of the window.
  const event = process.electronBinding('event').createEmpty();
  app.emit('offscreen-window-created', event, this);

  Object.defineProperty(this, 'devToolsWebContents', {
    enumerable: true,
    configurable: false,
    get () {
      return this.webContents.devToolsWebContents;
    }
  });
};

// Helpers.
Object.assign(OffscreenWindow.prototype, {
  loadURL (...args) {
    return this.webContents.loadURL(...args);
  },
  getURL (...args) {
    return this.webContents.getURL();
  },
  loadFile (...args) {
    return this.webContents.loadFile(...args);
  },
  reload (...args) {
    return this.webContents.reload(...args);
  },
  send (...args) {
    return this.webContents.send(...args);
  },
  openDevTools (...args) {
    return this.webContents.openDevTools(...args);
  },
  closeDevTools () {
    return this.webContents.closeDevTools();
  },
  isDevToolsOpened () {
    return this.webContents.isDevToolsOpened();
  },
  isDevToolsFocused () {
    return this.webContents.isDevToolsFocused();
  },
  toggleDevTools () {
    return this.webContents.toggleDevTools();
  },
  inspectElement (...args) {
    return this.webContents.inspectElement(...args);
  },
  inspectSharedWorker () {
    return this.webContents.inspectSharedWorker();
  },
  inspectServiceWorker () {
    return this.webContents.inspectServiceWorker();
  },
  showDefinitionForSelection () {
    return this.webContents.showDefinitionForSelection();
  },
  capturePage (...args) {
    return this.webContents.capturePage(...args);
  },
  setBackgroundThrottling (allowed) {
    this.webContents.setBackgroundThrottling(allowed);
  }
});

module.exports = OffscreenWindow;
