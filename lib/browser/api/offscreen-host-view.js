'use strict';

const electron = require('electron');

const { View } = electron;
const { OsrHostView } =
    process._linkedBinding('electron_browser_osr_host_view');
const OffscreenHostView = OsrHostView

Object.setPrototypeOf(OffscreenHostView.prototype, View.prototype);

module.exports = OffscreenHostView;
