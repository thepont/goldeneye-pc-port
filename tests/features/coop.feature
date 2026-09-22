Feature: PC co-op and multiplayer session lifecycle

  The PC port keeps solo missions and their authored setup data while adding
  a two-player co-op mode with independent cameras and safe spawn locations.

  Scenario: Launch a solo mission
    Given one fake controller
    When Dam is launched directly
    Then one player reaches the stage with one viewport

  Scenario: Launch a deathmatch session
    Given two fake controllers
    When a multiplayer session is launched at Facility
    Then the multiplayer setup reaches the stage with two viewports

  Scenario: Launch a co-op mission
    Given two fake controllers
    When the co-op menu is driven to Dam
    Then both players reach the stage at distinct positions
    And the top and bottom viewports are active

  Scenario: Resume solo
    Given one fake controller
    When a solo session is resumed at Dam
    Then one player reaches the stage with one viewport

  Scenario: Resume co-op with two controllers
    Given two fake controllers
    When a co-op session is resumed at Dam
    Then both players reach the stage at distinct positions
    And the top and bottom viewports are active

  Scenario: Resume co-op as a solo player
    Given one fake controller
    When a co-op session is resumed at Dam
    Then it safely falls back to one player and one viewport
