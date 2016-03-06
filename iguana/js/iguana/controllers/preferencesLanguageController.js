'use strict';

angular.module('iguanaApp.controllers').controller('preferencesLanguageController',
  function($scope, $rootScope, $log, $timeout, go, storageService) {

    this.availableLanguages = [{
      name: 'English',
      isoCode: 'en',
    }, {
      name: 'Français',
      isoCode: 'fr',
    }, {
      name: 'Deutsch',
      isoCode: 'de',
    }, {
      name: 'Español',
      isoCode: 'es',
    }, {
      name: '日本語',
      isoCode: 'ja',
      useIdeograms: true,
    }, {
      name: 'Pусский',
      isoCode: 'ru',
    }];

    this.save = function(newLang) {
      
      $rootScope.app_config = {
          wallet: {
            settings: {
              defaultLanguage: newLang
            }
          }
        };

      $log.debug('app_config: ', $rootScope.app_config);

      storageService.storeConfig($rootScope.app_config, function(err) {
        if (err) {
          $log.debug('storeConfig Lang error: ', err);
        }
        
        $log.debug('LangUpdated');
        
        go.preferences();
        
        $timeout(function() {
          $scope.$apply();
        }, 100);
      });
    
    };
  });
