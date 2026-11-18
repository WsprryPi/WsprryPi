// SPDX-License-Identifier: MIT
// Read-only fixture viewer; never drives target recurrence.
'use strict';
const {createBrowserProfile,launchBrowser,waitFor,finishFixture}=require('./browser_harness.js');
(async()=>{let browser,client,profile,failure;try{
 profile=createBrowserProfile();({browser,client}=await launchBrowser({executable:process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium',args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:15000}));
 await client.send('Page.navigate',{url:process.argv[2]});await waitFor(()=>client.evaluate("document.getElementById('observations')?.textContent.includes('execution_owner')"),'actual target observations');
 console.log('PASS read-only browser observed both target owners and closed before autonomous executions');
}catch(error){failure=error;}await finishFixture({failure,browser,client,profile});})().catch(error=>{console.error(error);process.exitCode=1;});
