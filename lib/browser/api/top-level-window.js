'use strict';

const electron = require('electron');
const { EventEmitter } = require('events');
const { TopLevelWindow } = process.electronBinding('top_level_window');

Object.setPrototypeOf(TopLevelWindow.prototype, EventEmitter.prototype);

const notOffscreenWindow = (win) => {
  return win && win.constructor.name !== 'OffscreenWindow';
};

TopLevelWindow.prototype._init = function () {
  // Avoid recursive require.
  const { app } = electron;

  const nativeGetAllWindows = this.getAllWindows
  this.getAllWindows = () => {
    return nativeGetAllWindows.filter(notOffscreenWindow)
  };

  // Simulate the application menu on platforms other than macOS.
  if (process.platform !== 'darwin') {
    const menu = app.applicationMenu;
    if (menu) this.setMenu(menu);
  }
};

TopLevelWindow.getFocusedWindow = () => {
  return TopLevelWindow.getAllWindows().find((win) => win.isFocused());
};

module.exports = TopLevelWindow;
