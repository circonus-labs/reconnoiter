-- Copyright (c) 2010-2017, Circonus, Inc.
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are
-- met:
--
--     * Redistributions of source code must retain the above copyright
--       notice, this list of conditions and the following disclaimer.
--     * Redistributions in binary form must reproduce the above
--       copyright notice, this list of conditions and the following
--       disclaimer in the documentation and/or other materials provided
--       with the distribution.
--     * Neither the name Circonus, Inc. nor the names of its contributors
--       may be used to endorse or promote products derived from this
--       software without specific prior written permission.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
-- "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
-- LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
-- A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
-- OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
-- SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
-- LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
-- DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
-- THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
-- (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
-- OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

module(..., package.seeall)

function onload(image)
    image.xml_description([=[
<module>
  <name>]=] .. image.name() .. [=[</name>
  <description><para>The googleanalytics v4 module gathers metrics via the Google Analytics export API v4.</para>
  <para><ulink url="http://code.google.com/apis/analytics/"><citetitle>Google Analytics</citetitle></ulink> provides access to Google Analytics data feeds over a lightweight API.</para>
  <para>This module rides on the http module and provides a secondary phase of XML parsing on the contents that extracts Google Analytics data into metrics that can be trended.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.googleanalytics4</object>
  <moduleconfig>
    <parameter name="api_key"
               required="optional"
               allowed=".+">The API Key used for username/password OAUTH 1.0 based authentication.</parameter>
    <parameter name="client_id"
               required="optional"
               allowed=".+">The Google-Generated client ID used for OAUTH 2.0 authentication.</parameter>
    <parameter name="client_secret"
               required="optional"
               allowed=".+">The Google-Generated client secret used for OAUTH 2.0 authentication.</parameter>
  </moduleconfig>
  <checkconfig>
    <parameter name="username"
               required="optional"
               allowed=".+">The username email address used to login to your Google Analytics account.</parameter>
    <parameter name="password"
               required="optional"
               allowed=".+">The password used to login to your Google Analytics account.</parameter>
    <parameter name="use_oauth"
               required="optional"
               allowed="^(?:true|false|on|off)$"
               default="false">Use OAuth authorization instead of username/password.</parameter>
    <parameter name="oauth_token"
               required="required"
               allowed=".+">The OAuth token used to access Google Analytics data.</parameter>
    <parameter name="oauth_token_secret"
               required="required"
               allowed=".+">The OAuth token secret key used to access Google Analytics data.</parameter>
    <parameter name="oauth_version"
               required="required"
               allowed=".+">The version of OAuth used.</parameter>
    <parameter name="table_id"
               required="required"
               allowed=".+">The table ID for the data feed to access.</parameter>
    <parameter name="metric_group"
               required="required"
               allowed=".+">The Metric Group (dimension) to import.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking Google Analytics v4 data for Acme, Inc.</title>
      <para>This example checks the Google Analytics v4 data feed for Acme, Inc.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="googleanalytics4" object="noit.module.googleanalytics4"/>
        </modules>
        <checks>
          <acme module="googleanalytics4">
            <check uuid="36b8ba72-7968-11dd-a67f-d39a2cc3f9de"/>
          </acme>
        </checks>
      </noit>
    ]]></programlisting>
    </example>
  </examples>
</module>
]=]);
    return 0
end

local cache_table = { }

-- default values for api key, client id, and client secret
-- these can be overwritten with the config file
local api_key       = ''
local client_id     = ''
local client_secret = ''

function init(module)
    cache_table = { }
    return 0
end

function config(module, options)
    -- api key is only used for non-oauth transactions
    -- or oauth 1.0 transactions... allow setting it,
    -- though in practice, this will probably never
    -- actually get set
    if options.api_key ~= nil then
        api_key = options.api_key
    end

    -- need a client id and client secret in order to
    -- use oauth. allow users to configure their own
    -- for inside deployments
    if options.client_id ~= nil then
        client_id = options.client_id
    end
    if options.client_secret ~= nil then
        client_secret = options.client_secret
    end

    return 0
end

local GoogleAnalytics = require 'noit.GoogleAnalytics4'

function initiate(module, check)
    local params = {
        username           = check.config.username,
        password           = check.config.password,
        use_oauth          = check.config.use_oauth,
        oauth_token        = check.config.oauth_token,
        oauth_token_secret = check.config.oauth_token_secret,
        table_id           = check.config.table_id,
        metric_group       = check.config.metric_group,
        oauth_version      = check.config.oauth_version,
        metrics            = {}
    }

    local uuid         = check.checkid
    local table_hit    = cache_table[uuid]
    local current_time = mtev.gettimeofday()

    if not table_hit then
        local new_hash = {}
        new_hash['timestamp'] = 0;
        new_hash['metrics'] = {};
        cache_table[uuid] = new_hash;
    end

    -- NOTE: v4 api endpoint only allows requesting 10 metrics per report.
    --       add additional elements to the params.metrics table to retrieve
    --       more than 10 metrics.
    if params.metric_group == 'User' then
        params.metrics = {
            {
                'users',
                'newUsers',
                'percentNewSessions',
                'sessionsPerUser'
            }
        }
    elseif params.metric_group == 'Session' then
        params.metrics = {
            {
                'sessions',
                'bounces',
                'bounceRate',
                'sessionDuration',
                'avgSessionDuration',
                'uniqueDimensionCombinations',
                'hits'
            }
        }
    elseif params.metric_group == 'TrafficSources' then
        params.metrics = {
            {
                'organicSearches'
            }
        }
    elseif params.metric_group == 'Adwords' then
        params.metrics = {
            {
                'impressions',
                'adClicks',
                'adCost',
                'CPM',
                'CPC',
                'CTR',
                'costPerTransaction',
                'costPerGoalConversion',
                'costPerConversion',
                'RPC'
            },
            {
                'ROAS'
            }
        }
    elseif params.metric_group == 'GoalConversions' then
        params.metrics = {
            {
                'goalStartsAll',
                'goalCompletionsAll',
                'goalValueAll',
                'goalValuePerSession',
                'goalConversionRateAll',
                'goalAbandonsAll',
                'goalAbandonRateAll'
            }
        }
    elseif params.metric_group == 'PageTracking' then
        params.metrics = {
            {
                'pageValue',
                'entrances',
                'entranceRate',
                'pageviews',
                'pageviewsPerSession',
                'uniquePageviews',
                'timeOnPage',
                'avgTimeOnPage',
                'exits',
                'exitRate'
            }
        }
    elseif params.metric_group == 'InternalSearch' then
        params.metrics = {
            {
                'searchResultViews',
                'searchUniques',
                'avgSearchResultViews',
                'searchSessions',
                'percentSessionsWithSearch',
                'searchDepth',
                'searchRefinements',
                'percentSearchRefinements',
                'searchDuration',
                'avgSearchDuration'
            },
            {
                'searchExits',
                'searchExitRate',
                'searchGoalConversionRateAll',
                'goalValueAllPerSearch'
            }
        }
    elseif params.metric_group == 'SiteSpeed' then
        params.metrics = {
            {
                'pageLoadTime',
                'pageLoadSample',
                'avgPageLoadTime',
                'domainLookupTime',
                'avgDomainLookupTime',
                'pageDownloadTime',
                'avgPageDownloadTime',
                'redirectionTime',
                'avgRedirectionTime',
                'serverConnectionTime'
            },
            {
                'avgServerConnectionTime',
                'serverResponseTime',
                'avgServerResponseTime',
                'speedMetricsSample',
                'domInteractiveTime',
                'avgDomInteractiveTime',
                'domContentLoadedTime',
                'avgDomContentLoadedTime',
                'domLatencyMetricsSample'
            }
        }
    elseif params.metric_group == 'AppTracking' then
        params.metrics = {
            {
                'screenviews',
                'uniqueScreenviews',
                'screenviewsPerSession',
                'timeOnScreen',
                'avgScreenviewDuration'
            }
        }
    elseif params.metric_group == 'EventTracking' then
        params.metrics = {
            {
                'totalEvents',
                'uniqueEvents',
                'eventValue',
                'avgEventValue',
                'sessionsWithEvent',
                'eventsPerSessionWithEvent'
            }
        }
    elseif params.metric_group == 'Ecommerce' then
        params.metrics = {
            {
                'transactions',
                'transactionsPerSession',
                'transactionRevenue',
                'revenuePerTransaction',
                'transactionRevenuePerSession',
                'transactionShipping',
                'transactionTax',
                'totalValue',
                'itemQuantity',
                'uniquePurchases'
            },
            {
                'revenuePerItem',
                'itemRevenue',
                'itemsPerPurchase',
                'localTransactionRevenue',
                'localTransactionShipping',
                'localTransactionTax',
                'localItemRevenue',
                'buyToDetailRate',
                'cartToDetailRate',
                'internalPromotionCTR'
            },
            {
                'internalPromotionClicks',
                'internalPromotionViews',
                'localProductRefundAmount',
                'localRefundAmount',
                'productAddsToCart',
                'productCheckouts',
                'productDetailViews',
                'productListCTR',
                'productListClicks',
                'productListViews'
            },
            {
                'productRefundAmount',
                'productRefunds',
                'productRemovesFromCart',
                'productRevenuePerPurchase',
                'quantityAddedToCart',
                'quantityCheckedOut',
                'quantityRefunded',
                'quantityRemovedFromCart',
                'refundAmount',
                'revenuePerUser'
            },
            {
                'totalRefunds',
                'transactionsPerUser'
            }
        }
    elseif params.metric_group == 'SocialInteractions' then
        params.metrics = {
            {
                'socialInteractions',
                'uniqueSocialInteractions',
                'socialInteractionsPerSession'
            }
        }
    elseif params.metric_group == 'UserTimings' then
        params.metrics = {
            {
                'userTimingValue',
                'userTImingSample',
                'avgUserTimingValue'
            }
        }
    elseif params.metric_group == 'Exceptions' then
        params.metrics = {
            {
                'exceptions',
                'exceptionsPerScreenview',
                'fatalExceptions',
                'fatalExceptionsPerScreenview'
            }
        }
    elseif params.metric_group == 'DoubleClickCampaignManager' then
        -- Restricted metrics - can only be queried under certain circumstances,
        -- unable to verify data received during testing...
        params.metrics = {
            {
                'dcmFloodlightQuantity',
                'dcmFloodlightRevenue',
                'dcmCPC',
                'dcmCTR',
                'dcmClicks',
                'dcmCost',
                'dcmImpressions',
                'dcmROAS',
                'dcmRPC'
            }
        }
    elseif params.metric_group == 'Audience' then
        -- Restricted metrics - can only be queried under certain circumstances,
        -- unable to verify data received during testing...
        params.metrics = {
            {
                'adsenseRevenue',
                'adsenseAdUnitsViewed',
                'adsenseAdsViewed',
                'adsenseAdsClicks',
                'adsensePageImpressions',
                'adsenseCTR',
                'adsenseECPM',
                'adsenseExits',
                'adsenseViewableImpressionPercent',
                'adsenseCoverage'
            }
        }
    elseif params.metric_group == 'AdExchange' then
        params.metrics = {
            {
                'adxImpressions',
                'adxCoverage',
                'adxMonetizedPageviews',
                'adxImpressionsPerSession',
                'adxViewableImpressionsPercent',
                'adxClicks',
                'adxCTR',
                'adxRevenue',
                'adxRevenuePer1000Sessions',
                'adxECPM'
            }
        }
    elseif params.metric_group == 'DoubleClickForPublishers' then
        -- Restricted metrics - can only be queried under certain circumstances,
        -- unable to verify data received during testing...
        params.metrics = {
            {
                'dfpImpressions',
                'dfpCoverage',
                'dfpMonetizedPageviews',
                'dfpImpressionsPerSession',
                'dfpViewableImpressionsPercent',
                'dfpClicks',
                'dfpCTR',
                'dfpRevenue',
                'dfpRevenuePer1000Sessions',
                'dfpECPM'
            }
        }
    elseif params.metric_group == 'DoubleClickForPublishersBackfill' then
        -- Restricted metrics - can only be queried under certain circumstances,
        -- unable to verify data received during testing...
        params.metrics = {
            {
                'backfillImpressions',
                'backfillCoverage',
                'backfillMonetizedPageviews',
                'backfillImpressionsPerSession',
                'backfillViewableImpressionsPercent',
                'backfillClicks',
                'backfillCTR',
                'backfillRevenue',
                'backfillRevenuePer1000Sessions',
                'backfillECPM'
            }
        }
    elseif params.metric_group == 'DoubleClickBidManager' then
        -- Restricted metrics - can only be queried under certain circumstances,
        -- unable to verify data received during testing...
        params.metrics = {
            {
                'dbmCPA',
                'dbmCPC',
                'dbmCPM',
                'dbmCTR',
                'dbmClicks',
                'dbmConversions',
                'dbmCost',
                'dbmImpressions',
                'dbmROAS'
            }
        }
    elseif params.metric_group == 'DoubleClickSearch' then
        -- Restricted metrics - can only be queried under certain circumstances,
        -- unable to verify data received during testing...
        params.metrics = {
            {
                'dsCPC',
                'dsCTR',
                'dsClicks',
                'dsCost',
                'dsImpressions',
                'dsProfit',
                'dsReturnOnAdSpend',
                'dsRevenuePerClick'
            }
        }
    else
        local err_msg = 'unknown googleanalytics4 metric group (' .. params.metric_group .. ')'
        mtev.log('error', '%s\n', err_msg)
        check.bad()
        check.unavailable()
        check.status(err_msg)
        return
    end

    -- assume the worst.
    check.bad()
    check.unavailable()

    local metric_count = 0
    local check_metric = function (name,value)
        if value == nil then
            check.metric_double(name, value)
        elseif tonumber(value) ~= nil then
            if string.find(value, "%.") ~= nil then
                check.metric_double(name, value)
            else
                check.metric_uint64(name, value)
            end
        else
            check.metric_string(name, value)
        end
        metric_count = metric_count + 1
    end

    -- Check about every 15 minutes
    if cache_table[uuid]['timestamp'] + (60 * 15) >= current_time then
        -- We've gone over the cache timeout... get new values
        cache_table[uuid]['metrics'] = {}
        local dns = mtev.dns()
        local r = dns:lookup('www.googleapis.com')
        if not r or r.a == nil then
            check.status("failed to resolve www.googleapis.com")
            cache_table[uuid]['timestamp'] = 0
            return
        end

        local ga = GoogleAnalytics:new(params, {})
        local rv, err = ga:perform(r.a, cache_table[uuid]['metrics'], api_key, client_id, client_secret)
        if rv ~= 0 then
            check.status(err or "unknown error")
            cache_table[uuid]['timestamp'] = 0
            return
        end
        cache_table[uuid]['timestamp'] = current_time
    end

    for k,v in pairs(cache_table[uuid]['metrics']) do
        check_metric(k, v)
    end

    check.available()
    if metric_count > 0 then
        check.good()
    else
        check.bad()
    end
    check.status("metrics=" .. metric_count)
end
