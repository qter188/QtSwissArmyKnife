﻿/*
 * Copyright 2018-2020 Qter(qsaker@qq.com). All rights reserved.
 *
 * The file is encoded using "utf8 with bom", it is a part
 * of QtSwissArmyKnife project.
 *
 * QtSwissArmyKnife is licensed according to the terms in
 * the file LICENCE in the root of the source code directory.
 */
#include <QFile>
#include <QDebug>
#include <QDateTime>
#include <QTextStream>
#include <QFileDialog>

#include "SAKDebugPage.hh"
#include "SAKCommonDataStructure.hh"
#include "SAKOutputSave2FileDialog.hh"
#include "SAKDebugPageOutputController.hh"

SAKDebugPageOutputController::SAKDebugPageOutputController(SAKDebugPage *debugPage, QObject *parent)
    :QThread(parent)
    ,mDebugPage(debugPage)
    ,mSettings(Q_NULLPTR)
    ,mSave2FileDialog(Q_NULLPTR)
    ,mRxAnimationgCount(5)
    ,mTxAnimationCount(0)
{
    // Initialize ui component
    mRxLabel = debugPage->mRxLabel;
    mTxLabel = debugPage->mTxLabel;
    mOutputTextFormatComboBox = debugPage->mOutputTextFormatComboBox;
    mShowDateCheckBox = debugPage->mShowDateCheckBox;
    mAutoWrapCheckBox = debugPage->mAutoWrapCheckBox;
    mShowTimeCheckBox = debugPage->mShowTimeCheckBox;
    mShowMsCheckBox = debugPage->mShowMsCheckBox;
    mShowRxDataCheckBox = debugPage->mShowRxDataCheckBox;
    mShowTxDataCheckBox = debugPage->mShowTxDataCheckBox;
    mSaveOutputToFileCheckBox = debugPage->mSaveOutputToFileCheckBox;
    mOutputFilePathPushButton = debugPage->mOutputFilePathPushButton;
    mClearOutputPushButton = debugPage->mClearOutputPushButton;
    mSaveOutputPushButton = debugPage->mSaveOutputPushButton;
    mOutputTextBroswer = debugPage->mOutputTextBroswer;
    mDebugPage->initOutputTextFormatComboBox(mOutputTextFormatComboBox);

    // Initializing setting keys
    QString group = mDebugPage->settingsGroup();
    mSettingStringOutputTextFormat = QString("%1/outputTextFormat").arg(group);
    mSettingStringShowDate = QString("%1/showDate").arg(group);
    mSettingStringAutoWrap = QString("%1/autoWrap").arg(group);
    mSettingStringShowTime = QString("%1/showTime").arg(group);
    mSettingStringShowMs = QString("%1/showMs").arg(group);
    mSettingStringShowRx = QString("%1/showRx").arg(group);
    mSettingStringShowTx = QString("%1/showTx").arg(group);

    // Readin settings before connecting signals and slots
    mSettings = mDebugPage->settings();
    readinSettings();

    // Connecting signals and slots
    connect(mSaveOutputToFileCheckBox, &QCheckBox::clicked, this, &SAKDebugPageOutputController::saveOutputDataToFile);
    connect(mAutoWrapCheckBox, &QCheckBox::clicked, this, &SAKDebugPageOutputController::setLineWrapMode);
    connect(mSaveOutputPushButton, &QCheckBox::clicked, this, &SAKDebugPageOutputController::saveOutputTextToFile);
    connect(mOutputFilePathPushButton, &QCheckBox::clicked, this, &SAKDebugPageOutputController::saveOutputDataSettings);
    connect(mOutputTextFormatComboBox, &QComboBox::currentTextChanged, this, &SAKDebugPageOutputController::onOutputTextFormatComboBoxCurrentTextChanged);
    connect(mShowDateCheckBox, &QCheckBox::clicked, this, &SAKDebugPageOutputController::onShowDateCheckBoxClicked);
    connect(mAutoWrapCheckBox, &QCheckBox::clicked, this, &SAKDebugPageOutputController::onAutoWrapCheckBoxClicked);
    connect(mShowTimeCheckBox, &QCheckBox::clicked, this, &SAKDebugPageOutputController::onShowTimeCheckBoxClicked);
    connect(mShowMsCheckBox, &QCheckBox::clicked, this, &SAKDebugPageOutputController::onShowMsCheckBoxClicked);
    connect(mShowRxDataCheckBox, &QCheckBox::clicked, this, &SAKDebugPageOutputController::onShowRxDataCheckBoxClicked);
    connect(mShowTxDataCheckBox, &QCheckBox::clicked, this, &SAKDebugPageOutputController::onShowTxDataCheckBoxClicked);

    // Input data
    connect(debugPage, &SAKDebugPage::bytesRead, this, &SAKDebugPageOutputController::bytesRead);
    connect(debugPage, &SAKDebugPage::bytesWritten, this, &SAKDebugPageOutputController::bytesWritten);

    // Output data
    connect(this, &SAKDebugPageOutputController::dataCooked, this, &SAKDebugPageOutputController::outputData);

    // Animation
    mUpdateRxAnimationTimer.setInterval(20);
    mUpdateTxAnimationTimer.setInterval(20);
    connect(&mUpdateRxAnimationTimer, &QTimer::timeout, this, &SAKDebugPageOutputController::updateRxAnimation);
    connect(&mUpdateTxAnimationTimer, &QTimer::timeout, this, &SAKDebugPageOutputController::updateTxAnimation);

    // Do something make memory happy
    mOutputTextBroswer->document()->setMaximumBlockCount(1000);

    // The class is used to save data to file
    mSave2FileDialog = new SAKOutputSave2FileDialog(mDebugPage);

    // The thread will started when the class is initailzed
    start();
}

SAKDebugPageOutputController::~SAKDebugPageOutputController()
{
    // Exit the thread first
    requestInterruption();
    mThreadWaitCondition.wakeAll();
    exit();
    wait();

    // Free memory
    delete mSave2FileDialog;
}

void SAKDebugPageOutputController::run()
{
    QEventLoop eventLoop;
    while (true) {
        // Cook data
        while (true) {
            RawDataStruct rawData = takeRawData();
            if (rawData.rawData.length()){
                innerCookData(rawData.rawData, rawData.parameters);
            }else{
                break;
            }
        }

        // Do something make thread inner happy
        eventLoop.processEvents();

        // If is interruption requested, the thread will exit, or the thread will sleep
        if (isInterruptionRequested()){
            break;
        }else{
            mThreadMutex.lock();
            mThreadWaitCondition.wait(&mThreadMutex, 50);
            mThreadMutex.unlock();
        }
    }
}

void SAKDebugPageOutputController::updateRxAnimation()
{
    mUpdateRxAnimationTimer.stop();
    mRxLabel->setText(QString("C%1").arg(QString(""), mRxAnimationgCount, '<'));

    mRxAnimationgCount -= 1;
    if (mRxAnimationgCount == -1){
        mRxAnimationgCount = 5;
    }
}

void SAKDebugPageOutputController::updateTxAnimation()
{
    mUpdateTxAnimationTimer.stop();
    mTxLabel->setText(QString("C%1").arg(QString(""), mTxAnimationCount, '>'));

    mTxAnimationCount += 1;
    if (mTxAnimationCount == 6){
        mTxAnimationCount = 0;
    }
}

void SAKDebugPageOutputController::setLineWrapMode()
{
    if (mAutoWrapCheckBox->isChecked()){
        mOutputTextBroswer->setLineWrapMode(QTextEdit::WidgetWidth);
    }else{
        mOutputTextBroswer->setLineWrapMode(QTextEdit::NoWrap);
    }
}

void SAKDebugPageOutputController::saveOutputTextToFile()
{
    QString outFileName = QFileDialog::getSaveFileName(Q_NULLPTR,
                                                       tr("Save to file"),
                                                       QString("./%1.txt")
                                                       .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss")),
                                                       QString("txt (*.txt)"));
    if (outFileName.isEmpty()){
        return;
    }

    QFile outFile(outFileName);
    if(outFile.open(QIODevice::WriteOnly|QIODevice::Text)){
        QTextStream outStream(&outFile);
        outStream << mOutputTextBroswer->toPlainText();
        outFile.flush();
        outFile.close();
    }else{
        mDebugPage->outputMessage(QString("Can not open file (%1) to save output data:")
                                 .arg(outFile.fileName()) + outFile.errorString(), false);
    }
}

void SAKDebugPageOutputController::saveOutputDataSettings()
{
    mSave2FileDialog->show();
}

void SAKDebugPageOutputController::saveOutputDataToFile()
{
    if (mSaveOutputToFileCheckBox->isChecked()){
        connect(mDebugPage, &SAKDebugPage::bytesRead, mSave2FileDialog, &SAKOutputSave2FileDialog::bytesRead);
        connect(mDebugPage, &SAKDebugPage::bytesWritten, mSave2FileDialog, &SAKOutputSave2FileDialog::bytesWritten);
    }else{
        disconnect(mDebugPage, &SAKDebugPage::bytesRead, mSave2FileDialog, &SAKOutputSave2FileDialog::bytesRead);
        disconnect(mDebugPage, &SAKDebugPage::bytesWritten, mSave2FileDialog, &SAKOutputSave2FileDialog::bytesWritten);
    }
}

void SAKDebugPageOutputController::bytesRead(QByteArray data)
{
    if (!mUpdateRxAnimationTimer.isActive()){
        mUpdateRxAnimationTimer.start();
    }

    if (!mShowRxDataCheckBox->isChecked()){
        return;
    }

    RawDataStruct rawData;
    OutputParameters parameters = outputDataParameters(true);
    rawData.rawData = data;
    rawData.parameters = parameters;
    mRawDataListMutex.lock();
    mRawDataList.append(rawData);
    mRawDataListMutex.unlock();

    // Wake the thead to handle raw data
    mThreadWaitCondition.wakeAll();
}

void SAKDebugPageOutputController::bytesWritten(QByteArray data)
{
    if (!mUpdateTxAnimationTimer.isActive()){
        mUpdateTxAnimationTimer.start();
    }

    if (!mShowTxDataCheckBox->isChecked()){
        return;
    }

    RawDataStruct rawData;
    OutputParameters parameters = outputDataParameters(false);
    rawData.rawData = data;
    rawData.parameters = parameters;
    mRawDataListMutex.lock();
    mRawDataList.append(rawData);
    mRawDataListMutex.unlock();

    // Wake the thead to handle raw data
    mThreadWaitCondition.wakeAll();
}

void SAKDebugPageOutputController::outputData(QString data)
{
    mOutputTextBroswer->append(data);
}

SAKDebugPageOutputController::OutputParameters SAKDebugPageOutputController::outputDataParameters(bool isReceivedData)
{
    OutputParameters parameters;
    parameters.showDate = mShowDateCheckBox->isChecked();
    parameters.showTime = mShowTimeCheckBox->isChecked();
    parameters.showMS = mShowMsCheckBox->isChecked();
    parameters.isReadData = isReceivedData;
    parameters.format= mOutputTextFormatComboBox->currentData().toInt();

    return parameters;
}

SAKDebugPageOutputController::RawDataStruct SAKDebugPageOutputController::takeRawData()
{
    RawDataStruct rawData;
    mRawDataListMutex.lock();
    if (mRawDataList.length()){
        rawData = mRawDataList.takeFirst();
    }
    mRawDataListMutex.unlock();

    return rawData;
}

void SAKDebugPageOutputController::readinSettings()
{
    auto setValue = [](QVariant &var){
        if (var.isNull()){
            return true;
        }else{
            return var.toBool();
        }
    };

    QVariant var = mSettings->value(mSettingStringOutputTextFormat);
    int index = 0;
    if (var.isNull()){
        index = 4;
    }else{
        index = var.toInt();
    }
    mOutputTextFormatComboBox->setCurrentIndex(index);

    var = mSettings->value(mSettingStringShowDate);
    bool value = mSettings->value(mSettingStringShowDate).toBool();
    mShowDateCheckBox->setChecked(value);

    var = mSettings->value(mSettingStringAutoWrap);
    value = setValue(var);
    mAutoWrapCheckBox->setChecked(value);

    var = mSettings->value(mSettingStringShowTime).toBool();
    mShowTimeCheckBox->setChecked(value);

    value = mSettings->value(mSettingStringShowMs).toBool();
    mShowMsCheckBox->setChecked(value);

    var = mSettings->value(mSettingStringShowRx);
    value = setValue(var);
    mShowRxDataCheckBox->setChecked(value);

    var = mSettings->value(mSettingStringShowTx);
    value = setValue(var);
    mShowTxDataCheckBox->setChecked(value);
}

void SAKDebugPageOutputController::innerCookData(QByteArray rawData, OutputParameters parameters)
{
    QString str;
    str.append("<font color=silver>[</font>");

    if (parameters.showDate){
        str.append(QDate::currentDate().toString("yyyy-MM-dd "));
        str = QString("<font color=silver>%1</font>").arg(str);
    }

    if (parameters.showTime){
        if (parameters.showMS){
            str.append(QTime::currentTime().toString("hh:mm:ss.zzz "));
        }else {
            str.append(QTime::currentTime().toString("hh:mm:ss "));
        }
        str = QString("<font color=silver>%1</font>").arg(str);
    }

    if (parameters.isReadData){
        str.append("<font color=red>Rx</font>");
    }else {
        str.append("<font color=blue>Tx</font>");
    }
    str.append("<font color=silver>] </font>");

    if (parameters.format == SAKCommonDataStructure::OutputFormatBin){
        for (int i = 0; i < rawData.length(); i++){
            str.append(QString("%1 ").arg(QString::number(static_cast<uint8_t>(rawData.at(i)), 2), 8, '0'));
        }
    }else if (parameters.format == SAKCommonDataStructure::OutputFormatOct){
        for (int i = 0; i < rawData.length(); i++){
            str.append(QString("%1 ").arg(QString::number(static_cast<uint8_t>(rawData.at(i)), 8), 3, '0'));
        }
    }else if (parameters.format == SAKCommonDataStructure::OutputFormatDec){
        for (int i = 0; i < rawData.length(); i++){
            str.append(QString("%1 ").arg(QString::number(static_cast<uint8_t>(rawData.at(i)), 10)));
        }
    }else if (parameters.format == SAKCommonDataStructure::OutputFormatHex){
        for (int i = 0; i < rawData.length(); i++){
            str.append(QString("%1 ").arg(QString::number(static_cast<uint8_t>(rawData.at(i)), 16), 2, '0'));
        }
    }else if (parameters.format == SAKCommonDataStructure::OutputFormatAscii){
        str.append(QString::fromLatin1(rawData));
    }else if (parameters.format == SAKCommonDataStructure::OutputFormatUtf8){
        str.append(QString::fromUtf8(rawData));
    }else if (parameters.format == SAKCommonDataStructure::OutputFormatUtf16){
        str.append(QString::fromUtf16(reinterpret_cast<const ushort*>(rawData.constData()),rawData.length()));
    }else if (parameters.format == SAKCommonDataStructure::OutputFormatUcs4){
        str.append(QString::fromUcs4(reinterpret_cast<const char32_t*>(rawData.constData()),rawData.length()));
    }else if (parameters.format == SAKCommonDataStructure::OutputFormatStdwstring){
        str.append(QString::fromWCharArray(reinterpret_cast<const wchar_t*>(rawData.constData()),rawData.length()));
    }else if (parameters.format == SAKCommonDataStructure::OutputFormatLocal){
        str.append(QString::fromLocal8Bit(rawData));
    }else {
        str.append(QString::fromUtf8(rawData));
        Q_ASSERT_X(false, __FUNCTION__, "Unknow output mode");
    }

    emit dataCooked(str);
}

void SAKDebugPageOutputController::onOutputTextFormatComboBoxCurrentTextChanged(const QString &text)
{
    Q_UNUSED(text);
    mSettings->setValue(mSettingStringOutputTextFormat, QVariant::fromValue(mOutputTextFormatComboBox->currentIndex()));
}

void SAKDebugPageOutputController::onShowDateCheckBoxClicked()
{
    mSettings->setValue(mSettingStringShowDate, QVariant::fromValue(mShowDateCheckBox->isChecked()));
}

void SAKDebugPageOutputController::onAutoWrapCheckBoxClicked()
{
    mSettings->setValue(mSettingStringAutoWrap, QVariant::fromValue(mAutoWrapCheckBox->isChecked()));
}

void SAKDebugPageOutputController::onShowTimeCheckBoxClicked()
{
    mSettings->setValue(mSettingStringShowTime, QVariant::fromValue(mShowTimeCheckBox->isChecked()));
}

void SAKDebugPageOutputController::onShowMsCheckBoxClicked()
{
    mSettings->setValue(mSettingStringShowMs, QVariant::fromValue(mShowMsCheckBox->isChecked()));
}

void SAKDebugPageOutputController::onShowRxDataCheckBoxClicked()
{
    mSettings->setValue(mSettingStringShowRx, QVariant::fromValue(mShowRxDataCheckBox->isChecked()));
}

void SAKDebugPageOutputController::onShowTxDataCheckBoxClicked()
{
    mSettings->setValue(mSettingStringShowTx, QVariant::fromValue(mShowTxDataCheckBox->isChecked()));
}
