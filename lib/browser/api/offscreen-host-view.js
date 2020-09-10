'use strict';

const electron = require('electron');

const { View } = electron;
const { OsrHostView } = process.electronBinding('osr_host_view');
const OffscreenHostView = OsrHostView

Object.setPrototypeOf(OffscreenHostView.prototype, View.prototype);

OffscreenHostView.prototype._init = function () {
  // Call parent class's _init.
  View.prototype._init.call(this);
};

module.exports = OffscreenHostView;
